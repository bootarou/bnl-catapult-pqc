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

#include "KeyPair.h"
#include "Ed25519Signer.h"
#include "MlDsa.h"
#include "iVrf.h"

namespace catapult { namespace crypto {

	// region MlDsaKeyPairTraits

	void MlDsaKeyPairTraits::ExtractPublicKeyFromPrivateKey(const PrivateKey& privateKey, PublicKey& publicKey) {
		ExtractMlDsaPublicKey(privateKey, publicKey);
	}

	// endregion

	// region VrfKeyPairTraits

	void VrfKeyPairTraits::ExtractPublicKeyFromPrivateKey(const PrivateKey& privateKey, PublicKey& publicKey) {
		// PQC iVRF: the vrf public key is the Merkle tree root derived from the seed at the default depth
		auto seed = iVrfSeed::FromBuffer(privateKey);
		iVrfKeyTree tree(seed, iVrf_Default_Tree_Depth);
		publicKey = tree.root().copyTo<VrfPublicKey>();
	}

	// endregion

	// region PrivateKeyUtils

	utils::ContainerHexFormatter<std::array<uint8_t, PrivateKey::Size>::const_iterator> PrivateKeyUtils::FormatPrivateKey(
			const PrivateKey& key) {
		return utils::HexFormat(key.begin(), key.end());
	}

	bool PrivateKeyUtils::IsValidPrivateKeyString(const std::string& str) {
		std::array<uint8_t, PrivateKey::Size> keyBuffer;
		return utils::TryParseHexStringIntoContainer(str.data(), str.size(), keyBuffer);
	}

	// endregion
}}
