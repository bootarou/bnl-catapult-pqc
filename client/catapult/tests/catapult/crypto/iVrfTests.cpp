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
		constexpr uint8_t Test_Depth = 10;
		constexpr uint64_t Test_Leaf_Count = static_cast<uint64_t>(1) << Test_Depth;

		iVrfSeed GenerateSeed() {
			return iVrfSeed::Generate(test::RandomByte);
		}
	}

	// region proof size

	TEST(TEST_CLASS, ProofHasFixedMaxSize) {
		// Assert: leaf + Max_Depth sibling hashes (independent of the configured depth)
		EXPECT_EQ(Hash256::Size * (1 + iVrf_Max_Tree_Depth), sizeof(iVrfProof));
	}

	// endregion

	// region prove / verify

	TEST(TEST_CLASS, ProofVerifiesForAllSampledIndices) {
		// Arrange:
		iVrfKeyTree tree(GenerateSeed(), Test_Depth);
		auto root = tree.root();

		// Act + Assert:
		for (uint64_t index : { uint64_t(0), uint64_t(1), uint64_t(123), Test_Leaf_Count / 2, Test_Leaf_Count - 1 }) {
			auto proof = tree.prove(index);
			EXPECT_TRUE(iVrfVerify(root, index, proof, Test_Depth)) << "index " << index;
		}
	}

	TEST(TEST_CLASS, VerifyFailsForWrongIndex) {
		// Arrange:
		iVrfKeyTree tree(GenerateSeed(), Test_Depth);
		auto proof = tree.prove(100);

		// Act + Assert:
		EXPECT_FALSE(iVrfVerify(tree.root(), 101, proof, Test_Depth));
	}

	TEST(TEST_CLASS, VerifyFailsForOutOfRangeIndex) {
		// Arrange:
		iVrfKeyTree tree(GenerateSeed(), Test_Depth);
		auto proof = tree.prove(0);

		// Act + Assert:
		EXPECT_FALSE(iVrfVerify(tree.root(), Test_Leaf_Count, proof, Test_Depth));
	}

	TEST(TEST_CLASS, VerifyFailsForWrongDepth) {
		// Arrange:
		iVrfKeyTree tree(GenerateSeed(), Test_Depth);
		auto proof = tree.prove(5);

		// Act + Assert:
		EXPECT_FALSE(iVrfVerify(tree.root(), 5, proof, Test_Depth + 1));
	}

	TEST(TEST_CLASS, VerifyFailsForTamperedLeaf) {
		// Arrange:
		iVrfKeyTree tree(GenerateSeed(), Test_Depth);
		auto proof = tree.prove(42);
		proof.Leaf[0] ^= 0x01;

		// Act + Assert:
		EXPECT_FALSE(iVrfVerify(tree.root(), 42, proof, Test_Depth));
	}

	TEST(TEST_CLASS, VerifyFailsForTamperedPath) {
		// Arrange:
		iVrfKeyTree tree(GenerateSeed(), Test_Depth);
		auto proof = tree.prove(42);
		proof.Path[0][0] ^= 0x01;

		// Act + Assert:
		EXPECT_FALSE(iVrfVerify(tree.root(), 42, proof, Test_Depth));
	}

	TEST(TEST_CLASS, VerifyFailsForProofFromDifferentSeed) {
		// Arrange:
		iVrfKeyTree tree1(GenerateSeed(), Test_Depth);
		iVrfKeyTree tree2(GenerateSeed(), Test_Depth);

		// Act + Assert: proof from tree2 does not verify against tree1's root
		EXPECT_FALSE(iVrfVerify(tree1.root(), 7, tree2.prove(7), Test_Depth));
	}

	TEST(TEST_CLASS, ProveThrowsForOutOfRangeIndex) {
		// Arrange:
		iVrfKeyTree tree(GenerateSeed(), Test_Depth);

		// Act + Assert:
		EXPECT_THROW(tree.prove(Test_Leaf_Count), catapult_invalid_argument);
	}

	TEST(TEST_CLASS, ConstructorThrowsForInvalidDepth) {
		// Act + Assert:
		EXPECT_THROW(iVrfKeyTree(GenerateSeed(), 0), catapult_invalid_argument);
		EXPECT_THROW(iVrfKeyTree(GenerateSeed(), iVrf_Max_Tree_Depth + 1), catapult_invalid_argument);
	}

	// endregion

	// region determinism

	TEST(TEST_CLASS, RootAndLeavesAreDeterministicForSameSeed) {
		// Arrange:
		auto seed = GenerateSeed();
		iVrfKeyTree tree1(seed, Test_Depth);
		iVrfKeyTree tree2(seed, Test_Depth);

		// Assert:
		EXPECT_EQ(tree1.root(), tree2.root());
		EXPECT_EQ(tree1.prove(123), tree2.prove(123));
		EXPECT_EQ(iVrfComputeLeaf(seed, 123), tree1.prove(123).Leaf);
	}

	// endregion

	// region generation hash

	TEST(TEST_CLASS, GenerationHashIsDeterministicAndBoundToInput) {
		// Arrange:
		iVrfKeyTree tree(GenerateSeed(), Test_Depth);
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
