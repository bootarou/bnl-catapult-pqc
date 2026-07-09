import { KeyPair } from './KeyPair.js';
import {
	MlKemCiphertextSize, decapsulateSharedKey, deriveMlKemPublicKey, encapsulateSharedKey
} from './SharedKey.js';
import { PublicKey } from '../CryptoTypes.js';
import {
	concatArrays, decodeAesGcmWithKey, encodeAesGcmWithKey
} from '../impl/CipherHelpers.js';
import { deepCompare } from '../utils/arrayHelpers.js';
import { hexToUint8, isHexString, uint8ToHex } from '../utils/converter.js';

const DELEGATION_MARKER = Uint8Array.from(Buffer.from('FE2A8061577301E2', 'hex'));

const filterExceptions = (statement, exceptions) => {
	try {
		const message = statement();
		return [true, message];
	} catch (exception) {
		if (!exceptions.some(exceptionMessage => exception.message.includes(exceptionMessage)))
			throw exception;
	}

	return [false, undefined];
};

const AES_GCM_EXCEPTIONS = [
	'Unsupported state or unable to authenticate data',
	'ML-KEM.decapsulate',
	'invalid ciphertext'
];

/**
 * Encrypts and encodes messages between two parties.
 * @note This uses ML-KEM-768 (FIPS 203) encapsulation to establish a shared key, replacing the
 *       ed25519/X25519 ECDH construction. Because ML-KEM is a KEM (not a Diffie-Hellman primitive),
 *       encoding requires the recipient's ML-KEM public key, which is derived deterministically from
 *       the recipient's account private key via `deriveMlKemPublicKey`.
 */
export default class MessageEncoder {
	/**
	 * Creates message encoder around key pair.
	 * @param {KeyPair} keyPair Key pair.
	 */
	constructor(keyPair) {
		/**
		 * @private
		 */
		this._keyPair = keyPair;
	}

	/**
	 * Public key used for message encoding.
	 * @returns {PublicKey} Public key used for message encoding.
	 */
	get publicKey() {
		return this._keyPair.publicKey;
	}

	/**
	 * ML-KEM public key of this encoder, to be shared with counterparties that want to send encrypted messages.
	 * @returns {Uint8Array} ML-KEM public key (1184 bytes).
	 */
	get mlKemPublicKey() {
		return deriveMlKemPublicKey(this._keyPair.privateKey);
	}

	/**
	 * Tries to decode an encoded message using this encoder's key pair.
	 * @param {PublicKey} recipientPublicKey Unused (retained for API compatibility); ML-KEM decoding
	 *                                       only requires this encoder's private key and the ciphertext.
	 * @param {Uint8Array} encodedMessage Encoded message.
	 * @returns {TryDecodeResult} Tuple containing decoded status and message.
	 */
	tryDecode(recipientPublicKey, encodedMessage) { // eslint-disable-line no-unused-vars
		if (1 === encodedMessage[0]) {
			const cipherTextEnd = 1 + MlKemCiphertextSize;
			const [result, message] = filterExceptions(
				() => {
					const sharedKey = decapsulateSharedKey(this._keyPair.privateKey, encodedMessage.subarray(1, cipherTextEnd));
					return decodeAesGcmWithKey(sharedKey, encodedMessage.subarray(cipherTextEnd));
				},
				AES_GCM_EXCEPTIONS
			);
			if (result)
				return { isDecoded: true, message };
		}

		if (0xFE === encodedMessage[0] && 0 === deepCompare(DELEGATION_MARKER, encodedMessage.slice(0, 8))) {
			const cipherTextStart = DELEGATION_MARKER.length;
			const cipherTextEnd = cipherTextStart + MlKemCiphertextSize;
			const [result, message] = filterExceptions(
				() => {
					const sharedKey = decapsulateSharedKey(this._keyPair.privateKey, encodedMessage.subarray(cipherTextStart, cipherTextEnd));
					return decodeAesGcmWithKey(sharedKey, encodedMessage.subarray(cipherTextEnd));
				},
				AES_GCM_EXCEPTIONS
			);
			if (result)
				return { isDecoded: true, message };
		}

		return { isDecoded: false, message: encodedMessage };
	}

	/**
	 * Encodes a message to a recipient using the recommended format.
	 * @param {Uint8Array} recipientMlKemPublicKey Recipient's ML-KEM public key (1184 bytes).
	 * @param {Uint8Array} message Message to encode.
	 * @returns {Uint8Array} Encrypted and encoded message.
	 */
	encode(recipientMlKemPublicKey, message) {
		const { cipherText, sharedKey } = encapsulateSharedKey(recipientMlKemPublicKey);
		const { tag, initializationVector, cipherText: encryptedMessage } = encodeAesGcmWithKey(sharedKey, message);

		return concatArrays(new Uint8Array([1]), cipherText, tag, initializationVector, encryptedMessage);
	}

	/**
	 * Encodes persistent harvesting delegation to node.
	 * @param {Uint8Array} nodeMlKemPublicKey Node's ML-KEM public key (1184 bytes).
	 * @param {KeyPair} remoteKeyPair Remote key pair.
	 * @param {KeyPair} vrfKeyPair Vrf key pair.
	 * @returns {Uint8Array} Encrypted and encoded harvesting delegation request.
	 */
	encodePersistentHarvestingDelegation(nodeMlKemPublicKey, remoteKeyPair, vrfKeyPair) {
		const message = concatArrays(remoteKeyPair.privateKey.bytes, vrfKeyPair.privateKey.bytes);
		const { cipherText, sharedKey } = encapsulateSharedKey(nodeMlKemPublicKey);
		const { tag, initializationVector, cipherText: encryptedMessage } = encodeAesGcmWithKey(sharedKey, message);

		return concatArrays(DELEGATION_MARKER, cipherText, tag, initializationVector, encryptedMessage);
	}

	/**
	 * Tries to decode encoded message.
	 * @deprecated This function is only provided for compatability with the original Symbol wallets.
	 *             Please use `tryDecode` in any new code.
	 * @param {PublicKey} recipientPublicKey Recipient's public key.
	 * @param {Uint8Array} encodedMessage Encoded message
	 * @returns {TryDecodeResult} Tuple containing decoded status and message.
	 */
	tryDecodeDeprecated(recipientPublicKey, encodedMessage) {
		const encodedHexString = new TextDecoder().decode(encodedMessage.subarray(1));
		if (1 === encodedMessage[0] && isHexString(encodedHexString)) {
			// wallet additionally hex encodes
			return this.tryDecode(recipientPublicKey, new Uint8Array([1, ...hexToUint8(encodedHexString)]));
		}

		return this.tryDecode(recipientPublicKey, encodedMessage);
	}

	/**
	 * Encodes message to recipient using (deprecated) wallet format.
	 * @deprecated This function is only provided for compatability with the original Symbol wallets.
	 *             Please use `encode` in any new code.
	 * @param {Uint8Array} recipientMlKemPublicKey Recipient's ML-KEM public key (1184 bytes).
	 * @param {Uint8Array} message Message to encode.
	 * @returns {Uint8Array} Encrypted and encoded message.
	 */
	encodeDeprecated(recipientMlKemPublicKey, message) {
		// wallet additionally hex encodes
		const encodedHexString = uint8ToHex(this.encode(recipientMlKemPublicKey, message).subarray(1));
		const encodedHexStringBytes = new TextEncoder().encode(encodedHexString);
		return new Uint8Array([1, ...encodedHexStringBytes]);
	}
}

// region type declarations

/**
 * Result of a try decode operation.
 * @class
 * @typedef {object} TryDecodeResult
 * @property {boolean} isDecoded \c true if message has been decoded and decrypted; \c false otherwise.
 * @property {Uint8Array} message Decoded message when `isDecoded` is \c true; encoded message otherwise.
 */

// endregion
