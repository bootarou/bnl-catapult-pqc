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

#include "Vrf.h"
#include "CryptoUtils.h"
#include "Ed25519Signer.h"
#include "Hashes.h"
#include "SecureZero.h"
#include <donna/catapult.h>

namespace catapult { namespace crypto {

	namespace {
		bool ge25519_unpack_positive_vartime(ge25519& A, const VrfPublicKey& publicKey) {
			// unpack public key
			if (!ge25519_unpack_negative_vartime(&A, publicKey.data()))
				return false;

			// negate A
			curve25519_neg(A.x, A.x);
			curve25519_neg(A.t, A.t);

			return true;
		}

		VrfPublicKey ScalarMultEight(const VrfPublicKey& key) {
			ge25519 A;
			if (!ge25519_unpack_positive_vartime(A, key))
				return VrfPublicKey();

			ge25519 B;
			ge25519_double(&B, &A);
			ge25519_double(&A, &B);
			ge25519_double(&B, &A);

			VrfPublicKey keyTimesEight;
			ge25519_pack(keyTimesEight.data(), &B);
			return keyTimesEight;
		}

		bool IsReduced(ScalarMultiplier& multiplier) {
			ScalarMultiplier reduced;
			bignum256modm temp;
			expand256_modm(temp, multiplier, 32);
			contract256_modm(reduced, temp);
			return 0 == std::memcmp(reduced, multiplier, 32);
		}

		Hash512 IetfHash(uint8_t suite, uint8_t action, std::initializer_list<const RawBuffer> buffersList) {
			Sha512_Builder builder;
			builder.update({ &suite, 1 });
			builder.update({ &action, 1 });
			for (const auto& buffer : buffersList)
				builder.update(buffer);

			Hash512 hash;
			builder.final(hash);
			return hash;
		}

		VrfPublicKey MapToKey(const RawBuffer& alpha, const VrfPublicKey& publicKey) {
			uint8_t i = 0;
			ge25519 A;
			while (true) {
				// Hash(suite | action | publicKey | alpha | i)
				auto hash = IetfHash(0x03, 0x01, { publicKey, alpha, { &i, 1 } });
				auto key = hash.copyTo<VrfPublicKey>();
				if (UnpackNegative(A, key))
					return ScalarMultEight(key);

				++i;
				if (0u == i)
					return VrfPublicKey();
			}
		}

		VrfPublicKey DoubleScalarMultVarTime(const ScalarMultiplier& encodedS, const VrfPublicKey& publicKey, const ScalarMultiplier& encodedC) {
			ge25519 A;
			if (!UnpackNegativeAndCheckSubgroup(A, publicKey))
				return VrfPublicKey();

			bignum256modm S;
			expand256_modm(S, encodedS, 32);

			bignum256modm C;
			expand256_modm(C, encodedC, 32);

			ge25519 ALIGN(16) R;
			ge25519_double_scalarmult_vartime(&R, &A, C, S);

			VrfPublicKey packedR;
			ge25519_pack(packedR.data(), &R);
			return packedR;
		}

		ProofVerificationHash VrfC(const VrfPublicKey& h, const VrfPublicKey& gamma, const VrfPublicKey& k_times_B, const VrfPublicKey& k_times_h) {
			auto hash = IetfHash(0x03, 0x02, { h, gamma, k_times_B, k_times_h });

			ProofVerificationHash c;
			std::memcpy(c.data(), hash.data(), 16);
			return c;
		}

		ProofScalar VrfS(const ScalarMultiplier& encodedK, const ProofVerificationHash& c, const ScalarMultiplier& encodedX) {
			bignum256modm C;
			expand256_modm(C, c.data(), c.Size);

			bignum256modm X;
			expand256_modm(X, encodedX, sizeof(ScalarMultiplier));

			bignum256modm CX;
			mul256_modm(CX, C, X);

			bignum256modm K;
			expand256_modm(K, encodedK, sizeof(ScalarMultiplier));

			bignum256modm S;
			add256_modm(S, K, CX);

			ProofScalar s;
			contract256_modm(s.data(), S);
			return s;
		}
	}

	VrfProof GenerateVrfProof(const RawBuffer& alpha, const VrfKeyPair& keyPair) {
		// map to group element
		auto h = MapToKey(alpha, keyPair.publicKey());
		if (VrfPublicKey() == h)
			return VrfProof();

		// gamma = x * h
		auto gamma = DeriveEd25519SharedSecret(keyPair.privateKey(), h);

		// generate k
		bignum256modm k;
		GenerateNonce(keyPair.privateKey(), { h }, k);

		// k * B
		VrfPublicKey k_times_B;
		ge25519 ALIGN(16) A;
		ge25519_scalarmult_base_niels(&A, ge25519_niels_base_multiples, k);
		ge25519_pack(k_times_B.data(), &A);

		// k * h
		VrfPublicKey k_times_h;
		ScalarMultiplier encodedK;
		contract256_modm(encodedK, k);
		ScalarMult(encodedK, h, k_times_h);

		// c = first 16 bytes of Sha512(suite | action | h | gamma | k * B | k * h)
		auto c = VrfC(h, gamma, k_times_B, k_times_h);

		// s = (k + cx) mod q
		ScalarMultiplier encodedX;
		ExtractMultiplier(keyPair.privateKey(), encodedX);
		auto s = VrfS(encodedK, c, encodedX);

		SecureZero(encodedK);
		SecureZero(encodedX);

		return { gamma.copyTo<ProofGamma>(), c, s };
	}

	Hash512 VerifyVrfProof(const VrfProof& vrfProof, const RawBuffer& alpha, const VrfPublicKey& publicKey) {
		auto gamma = vrfProof.Gamma.copyTo<VrfPublicKey>();

		// gamma must be on the curve
		ge25519 A;
		if (!UnpackNegative(A, gamma))
			return Hash512();

		// map to group element
		auto h = MapToKey(alpha, publicKey);
		if (VrfPublicKey() == h)
			return Hash512();

		// u = s * B - c * x_publicKey
		ScalarMultiplier encodedS;
		std::memcpy(encodedS, vrfProof.Scalar.data(), vrfProof.Scalar.size());
		if (!IsReduced(encodedS))
			return Hash512();

		ScalarMultiplier encodedC{};
		std::memcpy(encodedC, vrfProof.VerificationHash.data(), vrfProof.VerificationHash.size());
		auto u = DoubleScalarMultVarTime(encodedS, publicKey, encodedC);

		// V1 = s * h
		// scalar multiplication cannot fail because s is small enough
		VrfPublicKey v1;
		ScalarMult(encodedS, h, v1);

		// unpack cannot fail because V1 is valid by construction
		ge25519 V1;
		ge25519_unpack_positive_vartime(V1, v1);

		// V2 = -(c * gamma)
		// scalar multiplication cannot fail because c is small enough
		VrfPublicKey c_times_gamma;
		ScalarMult(encodedC, gamma, c_times_gamma);

		// unpack cannot fail because V2 is valid by construction
		ge25519 V2_p3;
		ge25519_unpack_negative_vartime(&V2_p3, c_times_gamma.data());

		ge25519_pniels V2_pniels;
		ge25519_full_to_pniels(&V2_pniels, &V2_p3);

		// v = V1 + V2_pniels = s * h - c * gamma
		ge25519 V;
		ge25519_p1p1 B;
		VrfPublicKey v;
		ge25519_pnielsadd_p1p1(&B, &V1, &V2_pniels, 0);
		ge25519_p1p1_to_full(&V, &B);
		ge25519_pack(v.data(), &V);

		// verificationHash = first 16 bytes of Sha512(suite | 0x2 | h | gamma | u | v)
		auto verificationHash = VrfC(h, gamma, u, v);
		return vrfProof.VerificationHash == verificationHash ? GenerateVrfProofHash(vrfProof.Gamma) : Hash512();
	}

	Hash512 GenerateVrfProofHash(const ProofGamma& gamma) {
		return IetfHash(0x03, 0x03, { ScalarMultEight(gamma.copyTo<VrfPublicKey>()) });
	}
}}
