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

#include "SharedKey.h"
#include "Hashes.h"
#include "SecureZero.h"
#include "catapult/exceptions.h"
#include <memory>
#include <cstring>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wreserved-id-macro"
#endif
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace catapult { namespace crypto {

	namespace {
		constexpr const char* Algorithm_Name = "ML-KEM-768";
		constexpr size_t Ml_Kem_Seed_Size = 64;

		struct EvpPkeyDeleter {
			void operator()(EVP_PKEY* pKey) const {
				EVP_PKEY_free(pKey);
			}
		};
		using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, EvpPkeyDeleter>;

		struct EvpPkeyCtxDeleter {
			void operator()(EVP_PKEY_CTX* pCtx) const {
				EVP_PKEY_CTX_free(pCtx);
			}
		};
		using EvpPkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, EvpPkeyCtxDeleter>;

		struct OsslParamDeleter {
			void operator()(OSSL_PARAM* pParams) const {
				OSSL_PARAM_free(pParams);
			}
		};
		using OsslParamPtr = std::unique_ptr<OSSL_PARAM, OsslParamDeleter>;

		struct OsslParamBldDeleter {
			void operator()(OSSL_PARAM_BLD* pBld) const {
				OSSL_PARAM_BLD_free(pBld);
			}
		};
		using OsslParamBldPtr = std::unique_ptr<OSSL_PARAM_BLD, OsslParamBldDeleter>;

		void DeriveMlKemSeed(const PrivateKey& privateKey, uint8_t (&mlKemSeed)[Ml_Kem_Seed_Size]) {
			// domain-separated expansion of the 32-byte private key into the 64-byte ML-KEM (d, z) seed
			constexpr auto Label = "catapult-mlkem-seed";
			Hash512 expandedSeed;
			Sha512_Builder builder;
			builder.update({ { reinterpret_cast<const uint8_t*>(Label), sizeof(Label) - 1 }, { privateKey.data(), privateKey.size() } });
			builder.final(expandedSeed);
			std::memcpy(mlKemSeed, expandedSeed.data(), Ml_Kem_Seed_Size);
			SecureZero(expandedSeed);
		}

		EvpPkeyPtr CreateKeyFromParams(const OSSL_PARAM* pParams, int selection) {
			EvpPkeyCtxPtr pCtx(EVP_PKEY_CTX_new_from_name(nullptr, Algorithm_Name, nullptr));
			if (!pCtx || EVP_PKEY_fromdata_init(pCtx.get()) <= 0)
				CATAPULT_THROW_RUNTIME_ERROR("ml-kem context initialization failed");

			EVP_PKEY* pRawKey = nullptr;
			if (EVP_PKEY_fromdata(pCtx.get(), &pRawKey, selection, const_cast<OSSL_PARAM*>(pParams)) <= 0)
				return nullptr;

			return EvpPkeyPtr(pRawKey);
		}

		EvpPkeyPtr CreateKeyPairFromSeed(const PrivateKey& privateKey) {
			uint8_t mlKemSeed[Ml_Kem_Seed_Size];
			DeriveMlKemSeed(privateKey, mlKemSeed);

			OsslParamBldPtr pBld(OSSL_PARAM_BLD_new());
			OSSL_PARAM_BLD_push_octet_string(pBld.get(), OSSL_PKEY_PARAM_ML_KEM_SEED, mlKemSeed, sizeof(mlKemSeed));
			OsslParamPtr pParams(OSSL_PARAM_BLD_to_param(pBld.get()));
			auto pKey = CreateKeyFromParams(pParams.get(), EVP_PKEY_KEYPAIR);
			SecureZero(mlKemSeed);
			if (!pKey)
				CATAPULT_THROW_RUNTIME_ERROR("ml-kem key generation from seed failed");

			return pKey;
		}
	}

	void MlKemKeyPairTraits::ExtractPublicKeyFromPrivateKey(const PrivateKey& privateKey, PublicKey& publicKey) {
		auto pKey = CreateKeyPairFromSeed(privateKey);

		size_t publicKeySize = 0;
		auto result = EVP_PKEY_get_octet_string_param(
				pKey.get(),
				OSSL_PKEY_PARAM_PUB_KEY,
				publicKey.data(),
				publicKey.size(),
				&publicKeySize);
		if (!result || MlKemPublicKey::Size != publicKeySize)
			CATAPULT_THROW_RUNTIME_ERROR("ml-kem public key extraction failed");
	}

	SharedKey Hkdf_Hmac_Sha256_32(const SharedSecret& sharedSecret) {
		Hash256 salt;
		Hash256 pseudoRandomKey;
		Hmac_Sha256(salt, sharedSecret, pseudoRandomKey);

		// specialized for single repetition, last byte contains counter value
		constexpr auto Buffer_Length = 8 + 1;
		std::array<uint8_t, Buffer_Length> buffer{ { 0x63, 0x61, 0x74, 0x61, 0x70, 0x75, 0x6C, 0x74, 0x01 } };

		Hash256 outputKeyingMaterial;
		Hmac_Sha256(pseudoRandomKey, buffer, outputKeyingMaterial);

		auto sharedKey = outputKeyingMaterial.copyTo<SharedKey>();
		SecureZero(pseudoRandomKey);
		SecureZero(outputKeyingMaterial);
		return sharedKey;
	}

	SharedKey EncapsulateSharedKey(const MlKemPublicKey& recipientPublicKey, MlKemCiphertext& ciphertext) {
		OsslParamBldPtr pBld(OSSL_PARAM_BLD_new());
		OSSL_PARAM_BLD_push_octet_string(pBld.get(), OSSL_PKEY_PARAM_PUB_KEY, recipientPublicKey.data(), recipientPublicKey.size());
		OsslParamPtr pParams(OSSL_PARAM_BLD_to_param(pBld.get()));
		auto pKey = CreateKeyFromParams(pParams.get(), EVP_PKEY_PUBLIC_KEY);
		if (!pKey)
			CATAPULT_THROW_RUNTIME_ERROR("ml-kem public key import failed");

		EvpPkeyCtxPtr pCtx(EVP_PKEY_CTX_new_from_pkey(nullptr, pKey.get(), nullptr));
		if (!pCtx || EVP_PKEY_encapsulate_init(pCtx.get(), nullptr) <= 0)
			CATAPULT_THROW_RUNTIME_ERROR("ml-kem encapsulate initialization failed");

		SharedSecret sharedSecret;
		auto ciphertextSize = ciphertext.size();
		auto sharedSecretSize = sharedSecret.size();
		if (EVP_PKEY_encapsulate(pCtx.get(), ciphertext.data(), &ciphertextSize, sharedSecret.data(), &sharedSecretSize) <= 0)
			CATAPULT_THROW_RUNTIME_ERROR("ml-kem encapsulation failed");

		if (MlKemCiphertext::Size != ciphertextSize || SharedSecret::Size != sharedSecretSize)
			CATAPULT_THROW_RUNTIME_ERROR("ml-kem encapsulation produced unexpected sizes");

		auto sharedKey = Hkdf_Hmac_Sha256_32(sharedSecret);
		SecureZero(sharedSecret);
		return sharedKey;
	}

	SharedKey DecapsulateSharedKey(const MlKemKeyPair& keyPair, const MlKemCiphertext& ciphertext) {
		auto pKey = CreateKeyPairFromSeed(keyPair.privateKey());

		EvpPkeyCtxPtr pCtx(EVP_PKEY_CTX_new_from_pkey(nullptr, pKey.get(), nullptr));
		if (!pCtx || EVP_PKEY_decapsulate_init(pCtx.get(), nullptr) <= 0)
			CATAPULT_THROW_RUNTIME_ERROR("ml-kem decapsulate initialization failed");

		SharedSecret sharedSecret;
		auto sharedSecretSize = sharedSecret.size();
		if (EVP_PKEY_decapsulate(pCtx.get(), sharedSecret.data(), &sharedSecretSize, ciphertext.data(), ciphertext.size()) <= 0)
			CATAPULT_THROW_RUNTIME_ERROR("ml-kem decapsulation failed");

		auto sharedKey = Hkdf_Hmac_Sha256_32(sharedSecret);
		SecureZero(sharedSecret);
		return sharedKey;
	}
}}
