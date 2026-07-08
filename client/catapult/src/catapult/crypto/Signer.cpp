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

#include "Signer.h"
#include "MlDsa.h"

namespace catapult { namespace crypto {

	// region Sign

	void Sign(const KeyPair& keyPair, const RawBuffer& dataBuffer, Signature& computedSignature) {
		Sign(keyPair, { dataBuffer }, computedSignature);
	}

	void Sign(const KeyPair& keyPair, std::initializer_list<const RawBuffer> buffersList, Signature& computedSignature) {
		MlDsaSign(keyPair.privateKey(), buffersList, computedSignature);
	}

	// endregion

	// region Verify

	bool Verify(const Key& publicKey, const RawBuffer& dataBuffer, const Signature& signature) {
		return Verify(publicKey, std::vector<RawBuffer>{ dataBuffer }, signature);
	}

	bool Verify(const Key& publicKey, const std::vector<RawBuffer>& buffers, const Signature& signature) {
		return MlDsaVerify(publicKey, buffers, signature);
	}

	// endregion

	// region VerifyMulti

	std::pair<std::vector<bool>, bool> VerifyMulti(const RandomFiller&, const SignatureInput* pSignatureInputs, size_t count) {
		// ML-DSA has no batch verification, so verify signatures individually
		auto aggregateResult = true;
		std::vector<bool> valid(count, true);
		for (auto i = 0u; i < count; ++i) {
			auto isValid = Verify(pSignatureInputs[i].PublicKey, pSignatureInputs[i].Buffers, pSignatureInputs[i].Signature);
			valid[i] = isValid;
			aggregateResult &= isValid;
		}

		return std::make_pair(std::move(valid), aggregateResult);
	}

	bool VerifyMultiShortCircuit(const RandomFiller&, const SignatureInput* pSignatureInputs, size_t count) {
		// short circuit on first failure
		for (auto i = 0u; i < count; ++i) {
			if (!Verify(pSignatureInputs[i].PublicKey, pSignatureInputs[i].Buffers, pSignatureInputs[i].Signature))
				return false;
		}

		return true;
	}

	// endregion
}}
