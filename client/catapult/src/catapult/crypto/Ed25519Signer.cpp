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

#include "Ed25519Signer.h"
#include "CryptoUtils.h"
#include "Hashes.h"
#include "SecureZero.h"
#include "catapult/exceptions.h"
#include <donna/catapult.h>

#ifdef _MSC_VER
#define RESTRICT __restrict
#else
#define RESTRICT __restrict__
#endif

namespace catapult { namespace crypto {

	namespace {
		const size_t Encoded_Size = Ed25519Signature::Size / 2;
		static_assert(Encoded_Size * 2 == Hash512::Size, "hash must be big enough to hold two encoded elements");

		// indicates that the encoded S part of the signature is less than the group order
		constexpr int Is_Reduced = 1;

		// indicates that the encoded S part of the signature is zero
		constexpr int Is_Zero = 2;

		void Reduce(uint8_t* out, const uint8_t* encodedS) {
			bignum256modm temp;
			expand_raw256_modm(temp, encodedS);
			reduce256_modm(temp);
			contract256_modm(out, temp);
		}

		int ValidateEncodedSPart(const uint8_t* encodedS) {
			uint8_t encodedBuf[Ed25519Signature::Size];
			uint8_t *RESTRICT encodedTempR = encodedBuf;
			uint8_t *RESTRICT encodedZero = encodedBuf + Encoded_Size;

			std::memset(encodedZero, 0, Encoded_Size);
			if (0 == std::memcmp(encodedS, encodedZero, Encoded_Size))
				return Is_Zero | Is_Reduced;

			Reduce(encodedTempR, encodedS);

			return std::memcmp(encodedTempR, encodedS, Encoded_Size) ? 0 : Is_Reduced;
		}

		bool IsCanonicalS(const uint8_t* encodedS) {
			return Is_Reduced == ValidateEncodedSPart(encodedS);
		}

		void CheckEncodedS(const uint8_t* encodedS) {
			if (0 == (ValidateEncodedSPart(encodedS) & Is_Reduced))
				CATAPULT_THROW_OUT_OF_RANGE("S part of signature invalid");
		}
	}

	void ExtractEd25519PublicKey(const PrivateKey& privateKey, VrfPublicKey& publicKey) {
		ed25519_publickey(privateKey.data(), publicKey.data());
	}

	void SignEd25519(
			const PrivateKey& privateKey,
			const VrfPublicKey& publicKey,
			std::initializer_list<const RawBuffer> buffersList,
			Ed25519Signature& computedSignature) {
		uint8_t *RESTRICT encodedR = computedSignature.data();
		uint8_t *RESTRICT encodedS = computedSignature.data() + Encoded_Size;

		// r = H(privHash[256:512] || data)
		// "EdDSA avoids these issues by generating r = H(h_b, ..., h_2b-1, M), so that
		//  different messages will lead to different, hard-to-predict values of r."
		bignum256modm r;
		GenerateNonce(privateKey, buffersList, r);

		// R = rModQ * base point
		ge25519 ALIGN(16) R;
		ge25519_scalarmult_base_niels(&R, ge25519_niels_base_multiples, r);
		ge25519_pack(encodedR, &R);

		// h = H(encodedR || public || data)
		Hash512 hash_h;
		Sha512_Builder hasher_h;
		hasher_h.update({ { encodedR, Encoded_Size }, publicKey });
		hasher_h.update(buffersList);
		hasher_h.final(hash_h);

		bignum256modm h;
		expand256_modm(h, hash_h.data(), 64);

		// hash the private key to improve randomness
		Hash512 privHash;
		HashPrivateKey(privateKey, privHash);

		// a = fieldElement(privHash[0:256])
		privHash[0] &= 0xF8;
		privHash[31] &= 0x7F;
		privHash[31] |= 0x40;

		bignum256modm a;
		expand256_modm(a, privHash.data(), 32);

		// S = (r + h * a) mod group order
		bignum256modm S;
		mul256_modm(S, h, a);
		add256_modm(S, S, r);
		contract256_modm(encodedS, S);

		// signature is (encodedR, encodedS)

		// throw if encodedS is not less than the group order, don't fail in case encodedS == 0
		// (this should only throw if there is a bug in the signing code)
		CheckEncodedS(encodedS);

		SecureZero(privHash);
		SecureZero(r);
		SecureZero(a);
	}

	bool VerifyEd25519(const VrfPublicKey& publicKey, const std::vector<RawBuffer>& buffers, const Ed25519Signature& signature) {
		const uint8_t *RESTRICT encodedR = signature.data();
		const uint8_t *RESTRICT encodedS = signature.data() + Encoded_Size;

		// reject if not canonical
		if (!IsCanonicalS(encodedS))
			return false;

		// reject zero public key, which is known weak key
		if (VrfPublicKey() == publicKey)
			return false;

		// h = H(encodedR || public || data)
		Hash512 hash_h;
		Sha512_Builder hasher_h;
		hasher_h.update({ { encodedR, Encoded_Size }, publicKey });
		for (const auto& buffer : buffers)
			hasher_h.update(buffer);

		hasher_h.final(hash_h);

		bignum256modm h;
		expand256_modm(h, hash_h.data(), 64);

		// A = -pub
		ge25519 ALIGN(16) A;
		if (!UnpackNegativeAndCheckSubgroup(A, publicKey))
			return false;

		bignum256modm S;
		expand256_modm(S, encodedS, 32);

		// R = encodedS * B - h * A
		ge25519 ALIGN(16) R;
		ge25519_double_scalarmult_vartime(&R, &A, h, S);

		// compare calculated R to given R
		uint8_t checkr[Encoded_Size];
		ge25519_pack(checkr, &R);
		return 1 == ed25519_verify(encodedR, checkr, 32);
	}

	VrfPublicKey DeriveEd25519SharedSecret(const PrivateKey& privateKey, const VrfPublicKey& otherPublicKey) {
		ScalarMultiplier multiplier;
		ExtractMultiplier(privateKey, multiplier);

		VrfPublicKey sharedSecret;
		if (!ScalarMult(multiplier, otherPublicKey, sharedSecret)) {
			SecureZero(multiplier);
			return VrfPublicKey();
		}

		SecureZero(multiplier);
		return sharedSecret;
	}
}}
