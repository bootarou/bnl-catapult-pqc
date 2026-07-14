/**
*** Copyright (c) 2016-2019, Jaguar0625, gimre, BloodyRookie, Tech Bureau, Corp.
*** Copyright (c) 2020-present, Jaguar0625, gimre, BloodyRookie.
*** All rights reserved.
***
*** This file is part of Catapult.
***
*** Catapult is free software: you can redistribute it and/or modify
*** it under the terms of the GNU Lesser General Public License as published by
*** the Free Software Foundation, either version 3 of the License, or
*** (at your option) any later version.
***
*** Catapult is distributed in the hope that it will be useful,
*** but WITHOUT ANY WARRANTY; without even the implied warranty of
*** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*** GNU Lesser General Public License for more details.
***
*** You should have received a copy of the GNU Lesser General Public License
*** along with Catapult. If not, see <http://www.gnu.org/licenses/>.
**/

#include "Harvester.h"
#include "catapult/cache_core/ImportanceView.h"
#include "catapult/cache_core/AccountStateCache.h"
#include "catapult/cache_core/AccountStateCacheUtils.h"
#include "catapult/chain/BlockDifficultyScorer.h"
#include "catapult/chain/BlockScorer.h"
#include "catapult/model/BlockUtils.h"
#include "catapult/utils/StackLogger.h"

namespace catapult { namespace harvesting {

	namespace {
		struct NextBlockContext {
		public:
			NextBlockContext(const model::BlockElement& parentBlockElement, Timestamp nextTimestamp)
					: ParentBlock(parentBlockElement.Block)
					, ParentContext(parentBlockElement)
					, Timestamp(nextTimestamp)
					, Height(ParentBlock.Height + catapult::Height(1))
					, BlockTime(utils::TimeSpan::FromDifference(Timestamp, ParentBlock.Timestamp))
			{}

		public:
			const model::Block& ParentBlock;
			model::PreviousBlockContext ParentContext;
			catapult::Timestamp Timestamp;
			catapult::Height Height;
			utils::TimeSpan BlockTime;
			catapult::Difficulty Difficulty;

		public:
			bool tryCalculateDifficulty(const cache::BlockStatisticCache& cache, const model::BlockchainConfiguration& config) {
				return chain::TryCalculateDifficulty(cache, ParentBlock.Height, config, Difficulty);
			}
		};

		std::unique_ptr<model::Block> CreateUnsignedBlockHeader(
				const NextBlockContext& context,
				model::EntityType blockType,
				model::NetworkIdentifier networkIdentifier,
				const Key& signer,
				const Address& beneficiary) {
			auto pBlock = model::CreateBlock(blockType, context.ParentContext, networkIdentifier, signer, {});
			pBlock->Difficulty = context.Difficulty;
			pBlock->Timestamp = context.Timestamp;
			if (Address() != beneficiary)
				pBlock->BeneficiaryAddress = beneficiary;

			return pBlock;
		}

		void AddGenerationHashProof(model::Block& block, const crypto::iVrfProof& iVrfProof) {
				block.GenerationHashProof = iVrfProof;
			}
	}

	Harvester::Harvester(
			const cache::CatapultCache& cache,
			const model::BlockchainConfiguration& config,
			const Address& beneficiary,
			const UnlockedAccounts& unlockedAccounts,
			const BlockGenerator& blockGenerator,
			const UnconfirmedTransactionsCountSupplier& unconfirmedTransactionsCountSupplier)
			: m_cache(cache)
			, m_config(config)
			, m_beneficiary(beneficiary)
			, m_unlockedAccounts(unlockedAccounts)
			, m_blockGenerator(blockGenerator)
			, m_unconfirmedTransactionsCountSupplier(unconfirmedTransactionsCountSupplier)
	{}

	const std::shared_ptr<crypto::iVrfKeyTree>& Harvester::getOrBuildiVrfTree(
			const BlockGeneratorAccountDescriptor& descriptor,
			uint8_t depth) {
		const auto& vrfPublicKey = descriptor.vrfKeyPair().publicKey();
		auto iter = m_iVrfTreeCache.find(vrfPublicKey);
		if (m_iVrfTreeCache.end() == iter) {
			// the iVRF seed is the vrf private key; the tree root is the registrable public value
			auto seed = crypto::iVrfSeed::FromBuffer(descriptor.vrfKeyPair().privateKey());
			iter = m_iVrfTreeCache.emplace(vrfPublicKey, std::make_shared<crypto::iVrfKeyTree>(seed, depth)).first;
		}

		return iter->second;
	}

	std::unique_ptr<model::Block> Harvester::harvest(const model::BlockElement& lastBlockElement, Timestamp timestamp) {
		NextBlockContext context(lastBlockElement, timestamp);

		// stop harvesting once the chain has been finalized (Height(0) disables finalization)
		if (Height(0) != m_config.ChainFinalizationHeight && context.Height > m_config.ChainFinalizationHeight) {
			CATAPULT_LOG(debug) << "skipping harvest attempt because chain is finalized at height " << m_config.ChainFinalizationHeight;
			return nullptr;
		}

		// empty block policy: when there are no pending transactions, skip the harvest attempt entirely.
		// this check runs BEFORE any iVRF work, so a skipped attempt neither computes a proof nor reveals a leaf.
		// TODO: v2 - refine the pending check from "unconfirmed cache is non-empty" to "at least one transaction
		//       is eligible for the next block" (fee multiplier, deadline, account eligibility).
		bool isEmptyBlockAllowed = model::EmptyBlockPolicyMode::Normal == m_config.EmptyBlockPolicy;
		if (!isEmptyBlockAllowed) {
			auto numPendingTransactions = m_unconfirmedTransactionsCountSupplier ? m_unconfirmedTransactionsCountSupplier() : 0;
			if (0 == numPendingTransactions) {
				bool isHeartbeatDue = model::EmptyBlockPolicyMode::Heartbeat == m_config.EmptyBlockPolicy
						&& context.BlockTime >= m_config.EmptyBlockHeartbeatInterval;
				if (!isHeartbeatDue) {
					CATAPULT_LOG(debug)
							<< "skipping empty block at height " << context.Height
							<< " (policy: " << (model::EmptyBlockPolicyMode::Heartbeat == m_config.EmptyBlockPolicy ? "heartbeat" : "suppress")
							<< ", no pending transactions, " << context.BlockTime << " since last block)";
					return nullptr;
				}

				CATAPULT_LOG(info)
						<< "generating heartbeat empty block at height " << context.Height
						<< " (" << context.BlockTime << " since last block >= interval " << m_config.EmptyBlockHeartbeatInterval << ")";
				isEmptyBlockAllowed = true;
			}
		}

		if (!context.tryCalculateDifficulty(m_cache.sub<cache::BlockStatisticCache>(), m_config)) {
			CATAPULT_LOG(debug) << "skipping harvest attempt due to error calculating difficulty";
			return nullptr;
		}

		chain::BlockHitContext hitContext;
		hitContext.ElapsedTime = context.BlockTime;
		hitContext.Difficulty = context.Difficulty;
		hitContext.Height = context.Height;

		const auto& accountStateCache = m_cache.sub<cache::AccountStateCache>();
		chain::BlockHitPredicate hitPredicate(m_config, [&accountStateCache](const auto& key, auto height) {
			auto lockedCacheView = accountStateCache.createView();
			cache::ReadOnlyAccountStateCache readOnlyCache(*lockedCacheView);
			cache::ImportanceView view(readOnlyCache);
			return view.getAccountImportanceOrDefault(key, height);
		});

		auto unlockedAccountsView = m_unlockedAccounts.view();
		const crypto::KeyPair* pHarvesterKeyPair = nullptr;
		crypto::iVrfProof iVrfProof;

		auto iVrfTreeDepth = m_config.IVrfTreeDepth;
		unlockedAccountsView.forEach([this, &context, &hitContext, &hitPredicate, &pHarvesterKeyPair, &iVrfProof,
				&accountStateCache, iVrfTreeDepth](const auto& descriptor) {
			hitContext.Signer = descriptor.signingKeyPair().publicKey();

			// resolve the iVRF registration (root + activation height) of the (forwarded) harvesting account
			Height activationHeight;
			bool hasRegistration = false;
			{
				auto accountStateView = accountStateCache.createView();
				cache::ReadOnlyAccountStateCache readOnlyCache(*accountStateView);
				auto accountStateIter = readOnlyCache.find(hitContext.Signer);
				if (accountStateIter.tryGet()) {
					cache::ProcessForwardedAccountState(readOnlyCache, accountStateIter.get().Address,
							[&activationHeight, &hasRegistration](const auto& forwardedAccountState) {
						activationHeight = forwardedAccountState.SupplementalPublicKeys.vrfRegistrationHeight();
						hasRegistration = static_cast<bool>(forwardedAccountState.SupplementalPublicKeys.vrf());
					});
				}
			}

			if (!hasRegistration || context.Height < activationHeight)
				return true;

			auto leafIndex = (context.Height - activationHeight).unwrap();
			if (leafIndex >= crypto::iVrfLeafCount(iVrfTreeDepth))
				return true;

			const auto& pTree = this->getOrBuildiVrfTree(descriptor, iVrfTreeDepth);
			iVrfProof = pTree->prove(leafIndex);
			hitContext.GenerationHash = crypto::iVrfGenerationHash(
					iVrfProof,
					{ context.ParentContext.GenerationHash.data(), context.ParentContext.GenerationHash.size() });
			if (hitPredicate(hitContext)) {
				pHarvesterKeyPair = &descriptor.signingKeyPair();
				return false;
			}

			return true;
		});

		if (!pHarvesterKeyPair)
			return nullptr;

		utils::StackLogger stackLogger("generating candidate block", utils::LogLevel::debug);
		auto pBlockHeader = CreateUnsignedBlockHeader(
				context,
				model::CalculateBlockTypeFromHeight(context.Height, m_config.ImportanceGrouping),
				m_config.Network.Identifier,
				pHarvesterKeyPair->publicKey(),
				m_beneficiary);

		AddGenerationHashProof(*pBlockHeader, iVrfProof);
		auto pBlock = m_blockGenerator(*pBlockHeader, m_config.MaxTransactionsPerBlock);
		if (pBlock && !isEmptyBlockAllowed) {
			// pending transactions can disappear (expire / get invalidated) between the pre-check
			// and generation; enforce the policy on the generated block as well
			auto transactions = pBlock->Transactions();
			if (transactions.cbegin() == transactions.cend()) {
				CATAPULT_LOG(debug) << "dropping generated empty block at height " << context.Height
						<< " (pending transactions became ineligible)";
				return nullptr;
			}
		}

		if (pBlock)
			SignBlockHeader(*pHarvesterKeyPair, *pBlock);

		return pBlock;
	}
}}
