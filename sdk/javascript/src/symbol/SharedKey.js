import { SharedKey256 } from '../CryptoTypes.js';
import { ml_kem768 } from '@noble/post-quantum/ml-kem.js';
import { hkdf } from '@noble/hashes/hkdf.js';
import { sha256, sha512 } from '@noble/hashes/sha2.js';
import { concatBytes, utf8ToBytes } from '@noble/hashes/utils.js';

/**
 * Byte size of an ML-KEM-768 (FIPS 203) public key.
 * @type {number}
 */
export const MlKemPublicKeySize = 1184;

/**
 * Byte size of an ML-KEM-768 (FIPS 203) ciphertext (encapsulated shared secret).
 * @type {number}
 */
export const MlKemCiphertextSize = 1088;

// domain-separation label used when expanding a 32-byte account seed into the 64-byte ML-KEM (d, z) seed
// note: must match the C++ node (crypto/SharedKey.cpp DeriveMlKemSeed)
const ML_KEM_SEED_LABEL = utf8ToBytes('catapult-mlkem-seed');

// HKDF label; matches the C++ Hkdf_Hmac_Sha256_32 (zeroed salt, info = "catapult", L = 32)
const HKDF_INFO = utf8ToBytes('catapult');

const deriveSharedKeyFromSecret = sharedSecret => new SharedKey256(hkdf(sha256, sharedSecret, undefined, HKDF_INFO, 32));

/**
 * Deterministically derives an ML-KEM-768 key pair from a 32-byte account private key (seed).
 * @param {PrivateKey} privateKey Account private key.
 * @returns {{ publicKey: Uint8Array, secretKey: Uint8Array }} ML-KEM key pair.
 */
const deriveMlKemKeyPair = privateKey => {
	const seed = sha512(concatBytes(ML_KEM_SEED_LABEL, privateKey.bytes)).subarray(0, 64);
	return ml_kem768.keygen(seed);
};

/**
 * Deterministically derives an account's ML-KEM-768 public key from its private key.
 * @param {PrivateKey} privateKey Account private key.
 * @returns {Uint8Array} ML-KEM public key (1184 bytes).
 */
const deriveMlKemPublicKey = privateKey => deriveMlKemKeyPair(privateKey).publicKey;

/**
 * Encapsulates a fresh shared key to a recipient's ML-KEM public key.
 * @param {Uint8Array} recipientMlKemPublicKey Recipient's ML-KEM public key (1184 bytes).
 * @returns {{ cipherText: Uint8Array, sharedKey: SharedKey256 }} Ciphertext (to transmit) and derived shared key.
 */
const encapsulateSharedKey = recipientMlKemPublicKey => {
	const { cipherText, sharedSecret } = ml_kem768.encapsulate(recipientMlKemPublicKey);
	return { cipherText, sharedKey: deriveSharedKeyFromSecret(sharedSecret) };
};

/**
 * Decapsulates a shared key from a ciphertext using an account private key.
 * @param {PrivateKey} privateKey Recipient's account private key.
 * @param {Uint8Array} cipherText ML-KEM ciphertext (1088 bytes).
 * @returns {SharedKey256} Shared encryption key.
 */
const decapsulateSharedKey = (privateKey, cipherText) => {
	const keyPair = deriveMlKemKeyPair(privateKey);
	const sharedSecret = ml_kem768.decapsulate(cipherText, keyPair.secretKey);
	return deriveSharedKeyFromSecret(sharedSecret);
};

export {
	deriveMlKemKeyPair,
	deriveMlKemPublicKey,
	encapsulateSharedKey,
	decapsulateSharedKey
};
