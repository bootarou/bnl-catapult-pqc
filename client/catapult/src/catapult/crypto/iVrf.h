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

	/// Depth of the iVRF Merkle tree; the tree covers 2^Depth block indices per registration.
	constexpr size_t iVrf_Tree_Depth = 16;

	/// Number of leaves (block indices) covered by a single iVRF registration.
	constexpr uint64_t iVrf_Leaf_Count = static_cast<uint64_t>(1) << iVrf_Tree_Depth;

	/// iVRF secret seed (32 bytes). Kept private by the harvester.
	struct iVrfSeed_tag { static constexpr size_t Size = 32; };
	using iVrfSeed = SecureByteArray<iVrfSeed_tag>;

	/// iVRF public root (32 bytes). Registered on chain in place of the ECVRF public key.
	using iVrfRoot = Hash256;

#pragma pack(push, 1)

	/// Fixed-size iVRF proof: revealed leaf plus its Merkle authentication path.
	struct iVrfProof {
		/// Revealed secret leaf for the block index.
		Hash256 Leaf;

		/// Merkle authentication path (sibling hashes from leaf to root).
		std::array<Hash256, iVrf_Tree_Depth> Path;

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
		/// Builds the full tree from \a seed.
		explicit iVrfKeyTree(const iVrfSeed& seed);

	public:
		/// Gets the tree root (the registrable public value).
		const iVrfRoot& root() const;

		/// Generates a proof for \a index (must be less than iVrf_Leaf_Count).
		iVrfProof prove(uint64_t index) const;

	private:
		// nodes stored level by level; m_levels[0] = leaves, m_levels[Depth] = { root }
		std::vector<std::vector<Hash256>> m_levels;
	};

	/// Recomputes the Merkle root implied by \a proof for \a index and returns \c true if it equals \a root.
	bool iVrfVerify(const iVrfRoot& root, uint64_t index, const iVrfProof& proof);

	/// Computes the iVRF output (block generation hash) binding \a proof's leaf to input \a alpha.
	GenerationHash iVrfGenerationHash(const iVrfProof& proof, const RawBuffer& alpha);
}}
