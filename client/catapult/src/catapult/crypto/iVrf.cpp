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

#include "iVrf.h"
#include "Hashes.h"
#include "catapult/exceptions.h"
#include <cstring>

namespace catapult { namespace crypto {

	namespace {
		constexpr uint8_t Leaf_Label[] = "catapult-ivrf-leaf";

		void WriteLe64(uint8_t (&buffer)[8], uint64_t value) {
			for (auto i = 0u; i < 8; ++i)
				buffer[i] = static_cast<uint8_t>(value >> (8 * i));
		}

		Hash256 HashPair(const Hash256& left, const Hash256& right) {
			Hash256 result;
			Sha3_256_Builder builder;
			builder.update({ { left.data(), left.size() }, { right.data(), right.size() } });
			builder.final(result);
			return result;
		}
	}

	bool iVrfProof::operator==(const iVrfProof& rhs) const {
		return Leaf == rhs.Leaf && Path == rhs.Path;
	}

	bool iVrfProof::operator!=(const iVrfProof& rhs) const {
		return !(*this == rhs);
	}

	Hash256 iVrfComputeLeaf(const iVrfSeed& seed, uint64_t index) {
		uint8_t indexBuffer[8];
		WriteLe64(indexBuffer, index);

		Hash256 leaf;
		Sha3_256_Builder builder;
		builder.update({
			{ Leaf_Label, sizeof(Leaf_Label) - 1 },
			{ seed.data(), seed.size() },
			{ indexBuffer, sizeof(indexBuffer) }
		});
		builder.final(leaf);
		return leaf;
	}

	iVrfKeyTree::iVrfKeyTree(const iVrfSeed& seed, uint8_t depth) : m_depth(depth) {
		if (0 == depth || depth > iVrf_Max_Tree_Depth)
			CATAPULT_THROW_INVALID_ARGUMENT_1("iVrf tree depth out of range", depth);

		m_levels.resize(depth + 1);

		// level 0: leaves
		auto leafCount = iVrfLeafCount(depth);
		auto& leaves = m_levels[0];
		leaves.reserve(leafCount);
		for (uint64_t i = 0; i < leafCount; ++i)
			leaves.push_back(iVrfComputeLeaf(seed, i));

		// internal levels
		for (size_t level = 0; level < depth; ++level) {
			const auto& lower = m_levels[level];
			auto& upper = m_levels[level + 1];
			upper.reserve(lower.size() / 2);
			for (size_t i = 0; i < lower.size(); i += 2)
				upper.push_back(HashPair(lower[i], lower[i + 1]));
		}
	}

	uint8_t iVrfKeyTree::depth() const {
		return m_depth;
	}

	const iVrfRoot& iVrfKeyTree::root() const {
		return m_levels[m_depth].front();
	}

	iVrfProof iVrfKeyTree::prove(uint64_t index) const {
		if (index >= iVrfLeafCount(m_depth))
			CATAPULT_THROW_INVALID_ARGUMENT_1("iVrf index out of range", index);

		iVrfProof proof;
		proof.Leaf = m_levels[0][index];
		// zero-pad unused trailing path slots
		for (auto& sibling : proof.Path)
			sibling = Hash256();

		auto nodeIndex = index;
		for (size_t level = 0; level < m_depth; ++level) {
			auto siblingIndex = nodeIndex ^ 1;
			proof.Path[level] = m_levels[level][siblingIndex];
			nodeIndex >>= 1;
		}

		return proof;
	}

	bool iVrfVerify(const iVrfRoot& root, uint64_t index, const iVrfProof& proof, uint8_t depth) {
		if (0 == depth || depth > iVrf_Max_Tree_Depth || index >= iVrfLeafCount(depth))
			return false;

		auto current = proof.Leaf;
		auto nodeIndex = index;
		for (size_t level = 0; level < depth; ++level) {
			// even node index => current is the left child, sibling on the right
			current = 0 == (nodeIndex & 1)
					? HashPair(current, proof.Path[level])
					: HashPair(proof.Path[level], current);
			nodeIndex >>= 1;
		}

		return current == root;
	}

	GenerationHash iVrfGenerationHash(const iVrfProof& proof, const RawBuffer& alpha) {
		Hash256 output;
		Sha3_256_Builder builder;
		builder.update({ { proof.Leaf.data(), proof.Leaf.size() }, alpha });
		builder.final(output);
		return output.copyTo<GenerationHash>();
	}
}}
