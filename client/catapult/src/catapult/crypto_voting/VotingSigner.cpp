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

#include "VotingSigner.h"
#include "catapult/crypto/Ed25519Signer.h"

namespace catapult { namespace crypto {

	void Sign(const VotingKeyPair& keyPair, const RawBuffer& dataBuffer, VotingSignature& computedSignature) {
		Sign(keyPair, { dataBuffer }, computedSignature);
	}

	void Sign(const VotingKeyPair& keyPair, std::initializer_list<const RawBuffer> buffersList, VotingSignature& computedSignature) {
		Ed25519Signature ed25519Signature;
		auto ed25519PrivateKey = PrivateKey::FromBuffer(keyPair.privateKey());
		auto ed25519PublicKey = keyPair.publicKey().copyTo<VrfPublicKey>();
		SignEd25519(ed25519PrivateKey, ed25519PublicKey, buffersList, ed25519Signature);

		computedSignature = ed25519Signature.copyTo<VotingSignature>();
	}

	bool Verify(const VotingKey& publicKey, const RawBuffer& dataBuffer, const VotingSignature& signature) {
		return Verify(publicKey, std::vector<RawBuffer>{ dataBuffer }, signature);
	}

	bool Verify(const VotingKey& publicKey, const std::vector<RawBuffer>& buffersList, const VotingSignature& signature) {
		auto ed25519PublicKey = publicKey.copyTo<VrfPublicKey>();
		auto ed25519Signature = signature.copyTo<Ed25519Signature>();
		return VerifyEd25519(ed25519PublicKey, buffersList, ed25519Signature);
	}
}}
