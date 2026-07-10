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
#include "SecureByteArray.h"
#include "catapult/types.h"
#include <array>
#include <vector>

namespace catapult { namespace crypto {

	/// Post-quantum, hash-based indexed VRF (iVRF) for the block-generation lottery.
	/// \note Replaces the edwards25519 ECVRF. Security rests only on the collision/preimage
	///       resistance of SHA3-256, so it is not broken by Shor's algorithm.
	///
	/// A harvester derives a fixed-depth Merkle tree of secret leaves from a 32-byte seed and
	/// registers the tree root on chain. For block index i (relative to the registration window),
	/// the harvester reveals leaf i together with its Merkle authentication path; the private
	/// per-height lottery value is H(leaf_i || parentGenerationHash). Uniqueness follows from the
	/// binding of leaf i to the registered root; privacy holds until the leaf is revealed.

	/// Maximum supported iVRF Merkle tree depth. Bounds the fixed on-wire proof size so that the
	/// configurable tree depth (\c BlockchainConfiguration::IVrfTreeDepth) can change without a
	/// block-header format change. The proof always reserves this many path slots; only the
	/// configured depth are meaningful.
	constexpr size_t iVrf_Max_Tree_Depth = 32;

	/// Default iVRF tree depth (matches BlockchainConfiguration::IVrfTreeDepth default). Used where the
	/// configured depth is unavailable (e.g. deriving the vrf public key = tree root from a key pair).
	constexpr uint8_t iVrf_Default_Tree_Depth = 16;

	/// iVRF secret seed (32 bytes). Kept private by the harvester.
	struct iVrfSeed_tag { static constexpr size_t Size = 32; };
	using iVrfSeed = SecureByteArray<iVrfSeed_tag>;

	/// iVRF public root (32 bytes). Registered on chain in place of the ECVRF public key.
	using iVrfRoot = Hash256;

	/// Number of leaves (block indices) covered by a single registration at \a depth.
	constexpr uint64_t iVrfLeafCount(uint8_t depth) {
		return static_cast<uint64_t>(1) << depth;
	}

#pragma pack(push, 1)

	/// Fixed-size iVRF proof: revealed leaf plus its Merkle authentication path.
	/// \note The path is sized to \c iVrf_Max_Tree_Depth; only the first (configured depth) entries are used.
	struct iVrfProof {
		/// Revealed secret leaf for the block index.
		Hash256 Leaf;

		/// Merkle authentication path (sibling hashes from leaf to root); trailing entries are zero-padded.
		std::array<Hash256, iVrf_Max_Tree_Depth> Path;

	public:
		/// Returns \c true if this proof is equal to \a rhs.
		bool operator==(const iVrfProof& rhs) const;

		/// Returns \c true if this proof is not equal to \a rhs.
		bool operator!=(const iVrfProof& rhs) const;
	};

#pragma pack(pop)

	/// Computes the iVRF leaf for \a index derived from \a seed.
	Hash256 iVrfComputeLeaf(const iVrfSeed& seed, uint64_t index);

	/// In-memory iVRF Merkle tree built from a seed; provides the root and per-index proofs.
	class iVrfKeyTree {
	public:
		/// Builds the tree of the specified \a depth from \a seed.
		iVrfKeyTree(const iVrfSeed& seed, uint8_t depth);

	public:
		/// Gets the tree depth.
		uint8_t depth() const;

		/// Gets the tree root (the registrable public value).
		const iVrfRoot& root() const;

		/// Generates a proof for \a index (must be less than 2^depth).
		iVrfProof prove(uint64_t index) const;

	private:
		uint8_t m_depth;
		// nodes stored level by level; m_levels[0] = leaves, m_levels[depth] = { root }
		std::vector<std::vector<Hash256>> m_levels;
	};

	/// Recomputes the depth-\a depth Merkle root implied by \a proof for \a index and returns \c true if it equals \a root.
	bool iVrfVerify(const iVrfRoot& root, uint64_t index, const iVrfProof& proof, uint8_t depth);

	/// Computes the iVRF output (block generation hash) binding \a proof's leaf to input \a alpha.
	GenerationHash iVrfGenerationHash(const iVrfProof& proof, const RawBuffer& alpha);
}}
