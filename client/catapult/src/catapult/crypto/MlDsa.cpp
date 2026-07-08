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

#include "MlDsa.h"
#include "catapult/exceptions.h"
#include <memory>

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
		constexpr const char* Algorithm_Name = "ML-DSA-44";

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

		struct EvpMdCtxDeleter {
			void operator()(EVP_MD_CTX* pCtx) const {
				EVP_MD_CTX_free(pCtx);
			}
		};
		using EvpMdCtxPtr = std::unique_ptr<EVP_MD_CTX, EvpMdCtxDeleter>;

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

		EvpPkeyPtr CreateKeyFromParams(const OSSL_PARAM* pParams, int selection) {
			EvpPkeyCtxPtr pCtx(EVP_PKEY_CTX_new_from_name(nullptr, Algorithm_Name, nullptr));
			if (!pCtx || EVP_PKEY_fromdata_init(pCtx.get()) <= 0)
				CATAPULT_THROW_RUNTIME_ERROR("ml-dsa context initialization failed");

			EVP_PKEY* pRawKey = nullptr;
			if (EVP_PKEY_fromdata(pCtx.get(), &pRawKey, selection, const_cast<OSSL_PARAM*>(pParams)) <= 0)
				return nullptr;

			return EvpPkeyPtr(pRawKey);
		}

		EvpPkeyPtr CreateKeyPairFromSeed(const PrivateKey& privateKey) {
			OsslParamBldPtr pBld(OSSL_PARAM_BLD_new());
			OSSL_PARAM_BLD_push_octet_string(pBld.get(), OSSL_PKEY_PARAM_ML_DSA_SEED, privateKey.data(), privateKey.size());
			OsslParamPtr pParams(OSSL_PARAM_BLD_to_param(pBld.get()));
			auto pKey = CreateKeyFromParams(pParams.get(), EVP_PKEY_KEYPAIR);
			if (!pKey)
				CATAPULT_THROW_RUNTIME_ERROR("ml-dsa key generation from seed failed");

			return pKey;
		}

		EvpPkeyPtr CreatePublicKey(const Key& publicKey) {
			OsslParamBldPtr pBld(OSSL_PARAM_BLD_new());
			OSSL_PARAM_BLD_push_octet_string(pBld.get(), OSSL_PKEY_PARAM_PUB_KEY, publicKey.data(), publicKey.size());
			OsslParamPtr pParams(OSSL_PARAM_BLD_to_param(pBld.get()));

			// note: returns nullptr when the encoded public key is rejected by openssl
			return CreateKeyFromParams(pParams.get(), EVP_PKEY_PUBLIC_KEY);
		}

		std::vector<uint8_t> ConcatenateBuffers(std::initializer_list<const RawBuffer> buffersList) {
			size_t totalSize = 0;
			for (const auto& buffer : buffersList)
				totalSize += buffer.Size;

			std::vector<uint8_t> message;
			message.reserve(totalSize);
			for (const auto& buffer : buffersList)
				message.insert(message.end(), buffer.pData, buffer.pData + buffer.Size);

			return message;
		}

		std::vector<uint8_t> ConcatenateBuffers(const std::vector<RawBuffer>& buffers) {
			size_t totalSize = 0;
			for (const auto& buffer : buffers)
				totalSize += buffer.Size;

			std::vector<uint8_t> message;
			message.reserve(totalSize);
			for (const auto& buffer : buffers)
				message.insert(message.end(), buffer.pData, buffer.pData + buffer.Size);

			return message;
		}
	}

	void ExtractMlDsaPublicKey(const PrivateKey& privateKey, Key& publicKey) {
		auto pKey = CreateKeyPairFromSeed(privateKey);

		size_t publicKeySize = 0;
		auto result = EVP_PKEY_get_octet_string_param(
				pKey.get(),
				OSSL_PKEY_PARAM_PUB_KEY,
				publicKey.data(),
				publicKey.size(),
				&publicKeySize);
		if (!result || Key::Size != publicKeySize)
			CATAPULT_THROW_RUNTIME_ERROR("ml-dsa public key extraction failed");
	}

	void MlDsaSign(const PrivateKey& privateKey, std::initializer_list<const RawBuffer> buffersList, Signature& computedSignature) {
		auto pKey = CreateKeyPairFromSeed(privateKey);
		auto message = ConcatenateBuffers(buffersList);

		// deterministic signing keeps block and nemesis generation byte-reproducible
		int deterministic = 1;
		OSSL_PARAM signParams[] = {
			OSSL_PARAM_construct_int(OSSL_SIGNATURE_PARAM_DETERMINISTIC, &deterministic),
			OSSL_PARAM_construct_end()
		};

		EvpMdCtxPtr pMdCtx(EVP_MD_CTX_new());
		if (!pMdCtx || EVP_DigestSignInit_ex(pMdCtx.get(), nullptr, nullptr, nullptr, nullptr, pKey.get(), signParams) <= 0)
			CATAPULT_THROW_RUNTIME_ERROR("ml-dsa sign initialization failed");

		size_t signatureSize = computedSignature.size();
		if (EVP_DigestSign(pMdCtx.get(), computedSignature.data(), &signatureSize, message.data(), message.size()) <= 0)
			CATAPULT_THROW_RUNTIME_ERROR("ml-dsa signing failed");

		if (Signature::Size != signatureSize)
			CATAPULT_THROW_RUNTIME_ERROR_1("ml-dsa produced unexpected signature size", signatureSize);
	}

	bool MlDsaVerify(const Key& publicKey, const std::vector<RawBuffer>& buffers, const Signature& signature) {
		// reject zero public key
		if (Key() == publicKey)
			return false;

		auto pKey = CreatePublicKey(publicKey);
		if (!pKey)
			return false;

		auto message = ConcatenateBuffers(buffers);

		EvpMdCtxPtr pMdCtx(EVP_MD_CTX_new());
		if (!pMdCtx || EVP_DigestVerifyInit_ex(pMdCtx.get(), nullptr, nullptr, nullptr, nullptr, pKey.get(), nullptr) <= 0)
			return false;

		return 1 == EVP_DigestVerify(pMdCtx.get(), signature.data(), signature.size(), message.data(), message.size());
	}
}}
