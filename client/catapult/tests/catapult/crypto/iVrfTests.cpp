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

#include "catapult/crypto/iVrf.h"
#include "tests/test/nodeps/Random.h"
#include "tests/TestHarness.h"

namespace catapult { namespace crypto {

#define TEST_CLASS iVrfTests

	namespace {
		iVrfSeed GenerateSeed() {
			return iVrfSeed::Generate(test::RandomByte);
		}
	}

	// region proof size

	TEST(TEST_CLASS, ProofHasFixedSize) {
		// Assert: leaf + Depth sibling hashes
		EXPECT_EQ(Hash256::Size * (1 + iVrf_Tree_Depth), sizeof(iVrfProof));
	}

	// endregion

	// region prove / verify

	TEST(TEST_CLASS, ProofVerifiesForAllSampledIndices) {
		// Arrange:
		iVrfKeyTree tree(GenerateSeed());
		auto root = tree.root();

		// Act + Assert:
		for (uint64_t index : { uint64_t(0), uint64_t(1), uint64_t(1234), iVrf_Leaf_Count / 2, iVrf_Leaf_Count - 1 }) {
			auto proof = tree.prove(index);
			EXPECT_TRUE(iVrfVerify(root, index, proof)) << "index " << index;
		}
	}

	TEST(TEST_CLASS, VerifyFailsForWrongIndex) {
		// Arrange:
		iVrfKeyTree tree(GenerateSeed());
		auto proof = tree.prove(100);

		// Act + Assert:
		EXPECT_FALSE(iVrfVerify(tree.root(), 101, proof));
	}

	TEST(TEST_CLASS, VerifyFailsForOutOfRangeIndex) {
		// Arrange:
		iVrfKeyTree tree(GenerateSeed());
		auto proof = tree.prove(0);

		// Act + Assert:
		EXPECT_FALSE(iVrfVerify(tree.root(), iVrf_Leaf_Count, proof));
	}

	TEST(TEST_CLASS, VerifyFailsForTamperedLeaf) {
		// Arrange:
		iVrfKeyTree tree(GenerateSeed());
		auto proof = tree.prove(42);
		proof.Leaf[0] ^= 0x01;

		// Act + Assert:
		EXPECT_FALSE(iVrfVerify(tree.root(), 42, proof));
	}

	TEST(TEST_CLASS, VerifyFailsForTamperedPath) {
		// Arrange:
		iVrfKeyTree tree(GenerateSeed());
		auto proof = tree.prove(42);
		proof.Path[0][0] ^= 0x01;

		// Act + Assert:
		EXPECT_FALSE(iVrfVerify(tree.root(), 42, proof));
	}

	TEST(TEST_CLASS, VerifyFailsForProofFromDifferentSeed) {
		// Arrange:
		iVrfKeyTree tree1(GenerateSeed());
		iVrfKeyTree tree2(GenerateSeed());

		// Act + Assert: proof from tree2 does not verify against tree1's root
		EXPECT_FALSE(iVrfVerify(tree1.root(), 7, tree2.prove(7)));
	}

	TEST(TEST_CLASS, ProveThrowsForOutOfRangeIndex) {
		// Arrange:
		iVrfKeyTree tree(GenerateSeed());

		// Act + Assert:
		EXPECT_THROW(tree.prove(iVrf_Leaf_Count), catapult_invalid_argument);
	}

	// endregion

	// region determinism

	TEST(TEST_CLASS, RootAndLeavesAreDeterministicForSameSeed) {
		// Arrange:
		auto seed = GenerateSeed();
		iVrfKeyTree tree1(seed);
		iVrfKeyTree tree2(seed);

		// Assert:
		EXPECT_EQ(tree1.root(), tree2.root());
		EXPECT_EQ(tree1.prove(123), tree2.prove(123));
		EXPECT_EQ(iVrfComputeLeaf(seed, 123), tree1.prove(123).Leaf);
	}

	// endregion

	// region generation hash

	TEST(TEST_CLASS, GenerationHashIsDeterministicAndBoundToInput) {
		// Arrange:
		iVrfKeyTree tree(GenerateSeed());
		auto proof = tree.prove(9);
		auto alpha1 = test::GenerateRandomByteArray<Hash256>();
		auto alpha2 = test::GenerateRandomByteArray<Hash256>();

		// Act:
		auto gh1a = iVrfGenerationHash(proof, { alpha1.data(), alpha1.size() });
		auto gh1b = iVrfGenerationHash(proof, { alpha1.data(), alpha1.size() });
		auto gh2 = iVrfGenerationHash(proof, { alpha2.data(), alpha2.size() });

		// Assert: deterministic for same input, differs for different input
		EXPECT_EQ(gh1a, gh1b);
		EXPECT_NE(gh1a, gh2);
	}

	// endregion
}}
