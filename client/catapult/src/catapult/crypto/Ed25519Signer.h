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
#include "KeyPair.h"
#include <vector>

namespace catapult { namespace crypto {

	/// ed25519 signature.
	/// \note ed25519 is retained exclusively for the (pre-quantum) VRF scheme and finalization voting;
	///       the primary account signature scheme is ML-DSA-44 (see Signer.h).
	struct Ed25519Signature_tag { static constexpr size_t Size = 64; };
	using Ed25519Signature = utils::ByteArray<Ed25519Signature_tag>;

	/// Extracts an ed25519 public key (\a publicKey) from \a privateKey.
	void ExtractEd25519PublicKey(const PrivateKey& privateKey, VrfPublicKey& publicKey);

	/// Signs data in \a buffersList using \a privateKey and \a publicKey, placing the result in \a computedSignature.
	/// \note The function will throw if the generated S part of the signature is not less than the group order.
	void SignEd25519(
			const PrivateKey& privateKey,
			const VrfPublicKey& publicKey,
			std::initializer_list<const RawBuffer> buffersList,
			Ed25519Signature& computedSignature);

	/// Verifies that \a signature of data in \a buffers is valid, using ed25519 public key \a publicKey.
	bool VerifyEd25519(const VrfPublicKey& publicKey, const std::vector<RawBuffer>& buffers, const Ed25519Signature& signature);

	/// Derives an ed25519 (x25519-style) shared secret from \a privateKey and \a otherPublicKey.
	/// \note Used by the VRF implementation (gamma = x * h).
	VrfPublicKey DeriveEd25519SharedSecret(const PrivateKey& privateKey, const VrfPublicKey& otherPublicKey);
}}
