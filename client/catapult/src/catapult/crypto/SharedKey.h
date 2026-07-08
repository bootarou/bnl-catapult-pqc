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

namespace catapult { namespace crypto {

	struct SharedKey_tag { static constexpr size_t Size = 32; };
	using SharedKey = utils::ByteArray<SharedKey_tag>;

	struct SharedSecret_tag { static constexpr size_t Size = 32; };
	using SharedSecret = utils::ByteArray<SharedSecret_tag>;

	/// ML-KEM-768 (FIPS 203) public key.
	struct MlKemPublicKey_tag { static constexpr size_t Size = 1184; };
	using MlKemPublicKey = utils::ByteArray<MlKemPublicKey_tag>;

	/// ML-KEM-768 (FIPS 203) ciphertext (encapsulated shared secret).
	struct MlKemCiphertext_tag { static constexpr size_t Size = 1088; };
	using MlKemCiphertext = utils::ByteArray<MlKemCiphertext_tag>;

	/// ML-KEM-768 key pair traits.
	/// \note The 32-byte private key is expanded into the 64-byte ML-KEM (d, z) seed via domain-separated hashing.
	struct MlKemKeyPairTraits {
	public:
		using PublicKey = MlKemPublicKey;
		using PrivateKey = crypto::PrivateKey;

	public:
		/// Extracts a public key (\a publicKey) from a private key (\a privateKey).
		static void ExtractPublicKeyFromPrivateKey(const PrivateKey& privateKey, PublicKey& publicKey);
	};

	/// ML-KEM-768 key pair.
	using MlKemKeyPair = BasicKeyPair<MlKemKeyPairTraits>;

	/// Generates HKDF of \a sharedSecret using default zeroed salt and constant label "catapult".
	SharedKey Hkdf_Hmac_Sha256_32(const SharedSecret& sharedSecret);

	/// Generates a shared key by encapsulating a fresh shared secret to \a recipientPublicKey.
	/// The encapsulated secret is written to \a ciphertext, which must be transmitted alongside the encrypted payload.
	/// \note This replaces the (ephemeral public key, ECDH) construction used with ed25519.
	SharedKey EncapsulateSharedKey(const MlKemPublicKey& recipientPublicKey, MlKemCiphertext& ciphertext);

	/// Generates a shared key by decapsulating \a ciphertext with \a keyPair.
	/// \note Per FIPS 203 implicit rejection, an invalid ciphertext yields a pseudo-random key;
	///       authenticity is enforced by the subsequent AEAD tag check.
	SharedKey DecapsulateSharedKey(const MlKemKeyPair& keyPair, const MlKemCiphertext& ciphertext);
}}
