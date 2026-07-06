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

#include "src/validators/Validators.h"
#include "catapult/validators/ValidatorContext.h"
#include "tests/test/cache/CacheTestUtils.h"
#include "tests/test/core/NotificationTestUtils.h"
#include "tests/test/plugins/ValidatorTestUtils.h"
#include "tests/TestHarness.h"

namespace catapult { namespace validators {

#define TEST_CLASS ChainFinalizationValidatorTests

	DEFINE_COMMON_VALIDATOR_TESTS(ChainFinalization, Height(123))

	namespace {
		constexpr auto Success_Result = ValidationResult::Success;
		constexpr auto Failure_Result = Failure_Core_Chain_Finalization_Height_Exceeded;

		void AssertValidationResult(ValidationResult expectedResult, Height chainFinalizationHeight, Height blockHeight) {
			// Arrange:
			auto cache = test::CreateEmptyCatapultCache();
			auto notification = test::CreateBlockNotification();
			auto pValidator = CreateChainFinalizationValidator(chainFinalizationHeight);

			// Act:
			auto result = test::ValidateNotification(*pValidator, notification, cache, blockHeight);

			// Assert:
			EXPECT_EQ(expectedResult, result)
					<< "chainFinalizationHeight " << chainFinalizationHeight << ", blockHeight " << blockHeight;
		}
	}

	// region disabled (Height(0))

	TEST(TEST_CLASS, SuccessWhenFinalizationIsDisabled) {
		// Assert: Height(0) disables finalization, so all block heights are accepted
		AssertValidationResult(Success_Result, Height(0), Height(1));
		AssertValidationResult(Success_Result, Height(0), Height(100));
		AssertValidationResult(Success_Result, Height(0), Height(1'000'000));
	}

	// endregion

	// region enabled

	TEST(TEST_CLASS, SuccessWhenBlockHeightIsBelowFinalizationHeight) {
		AssertValidationResult(Success_Result, Height(100), Height(1));
		AssertValidationResult(Success_Result, Height(100), Height(50));
		AssertValidationResult(Success_Result, Height(100), Height(99));
	}

	TEST(TEST_CLASS, SuccessWhenBlockHeightEqualsFinalizationHeight) {
		// Assert: the finalization height itself is the last accepted block
		AssertValidationResult(Success_Result, Height(100), Height(100));
	}

	TEST(TEST_CLASS, FailureWhenBlockHeightExceedsFinalizationHeight) {
		AssertValidationResult(Failure_Result, Height(100), Height(101));
		AssertValidationResult(Failure_Result, Height(100), Height(200));
		AssertValidationResult(Failure_Result, Height(100), Height(1'000'000));
	}

	// endregion
}}
