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

	/// ML-DSA-44 (FIPS 204) primitives implemented via openssl.
	/// \note The 32-byte PrivateKey is the ML-DSA seed (xi); key expansion is deterministic.

	/// Extracts an ML-DSA-44 public key (\a publicKey) from a seed (\a privateKey).
	void ExtractMlDsaPublicKey(const PrivateKey& privateKey, Key& publicKey);

	/// Signs data in \a buffersList using seed (\a privateKey), placing the result in \a computedSignature.
	/// \note Signing is deterministic in order to keep block/nemesis generation reproducible.
	void MlDsaSign(const PrivateKey& privateKey, std::initializer_list<const RawBuffer> buffersList, Signature& computedSignature);

	/// Verifies that \a signature of data in \a buffers is valid, using public key \a publicKey.
	bool MlDsaVerify(const Key& publicKey, const std::vector<RawBuffer>& buffers, const Signature& signature);
}}
