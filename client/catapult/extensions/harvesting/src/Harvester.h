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

#pragma once
#include "HarvesterBlockGenerator.h"
#include "UnlockedAccounts.h"
#include "catapult/cache/CatapultCache.h"
#include "catapult/crypto/iVrf.h"
#include "catapult/functions.h"
#include "catapult/model/BlockchainConfiguration.h"
#include "catapult/model/Elements.h"
#include "catapult/model/EntityInfo.h"
#include "catapult/utils/Hashers.h"
#include <memory>
#include <unordered_map>

namespace catapult { namespace harvesting { struct BlockExecutionHashes; } }

namespace catapult { namespace harvesting {

	/// Supplies the number of transactions currently waiting in the unconfirmed transactions cache.
	using UnconfirmedTransactionsCountSupplier = supplier<size_t>;

	/// Harvests new blocks.
	class Harvester {
	public:
		/// Creates a harvester around catapult \a cache, blockchain \a config, \a beneficiary,
		/// unlocked accounts set (\a unlockedAccounts) and \a blockGenerator used to customize block generation.
		/// \a unconfirmedTransactionsCountSupplier feeds the empty block policy; when not set,
		/// the pending transactions count is assumed to be zero.
		Harvester(
				const cache::CatapultCache& cache,
				const model::BlockchainConfiguration& config,
				const Address& beneficiary,
				const UnlockedAccounts& unlockedAccounts,
				const BlockGenerator& blockGenerator,
				const UnconfirmedTransactionsCountSupplier& unconfirmedTransactionsCountSupplier = UnconfirmedTransactionsCountSupplier());

	public:
		/// Creates the best block (if any) harvested by any unlocked account.
		/// Created block will have \a lastBlockElement as parent and \a timestamp as timestamp.
		std::unique_ptr<model::Block> harvest(const model::BlockElement& lastBlockElement, Timestamp timestamp);

	private:
		const std::shared_ptr<crypto::iVrfKeyTree>& getOrBuildiVrfTree(const BlockGeneratorAccountDescriptor& descriptor, uint8_t depth);

	private:
		const cache::CatapultCache& m_cache;
		const model::BlockchainConfiguration m_config;
		const Address m_beneficiary;
		const UnlockedAccounts& m_unlockedAccounts;
		BlockGenerator m_blockGenerator;
		UnconfirmedTransactionsCountSupplier m_unconfirmedTransactionsCountSupplier;

		// cache of built iVRF Merkle trees keyed by vrf public key; building a tree is expensive (O(2^depth))
		// so it is reused across harvest attempts for the same account
		std::unordered_map<VrfPublicKey, std::shared_ptr<crypto::iVrfKeyTree>, utils::ArrayHasher<VrfPublicKey>> m_iVrfTreeCache;
	};
}}
