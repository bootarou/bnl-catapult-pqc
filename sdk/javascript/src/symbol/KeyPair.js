import { PrivateKey, PublicKey, Signature } from '../CryptoTypes.js';
import { deepCompare } from '../utils/arrayHelpers.js';
import { ml_dsa44 } from '@noble/post-quantum/ml-dsa.js';

/**
 * Represents an ML-DSA-44 (FIPS 204) private and public key.
 * @note the 32-byte private key is the ML-DSA seed; key expansion is deterministic.
 */
export class KeyPair {
	/**
	 * Creates a key pair from a private key.
	 * @param {PrivateKey} privateKey Private key.
	 */
	constructor(privateKey) {
		/**
		 * @private
		 */
		this._privateKey = privateKey;

		/**
		 * @private
		 */
		this._keyPair = ml_dsa44.keygen(this._privateKey.bytes);
	}

	/**
	 * Gets the public key.
	 * @returns {PublicKey} Public key.
	 */
	get publicKey() {
		return new PublicKey(this._keyPair.publicKey);
	}

	/**
	 * Gets the private key.
	 * @returns {PrivateKey} Private key.
	 */
	get privateKey() {
		return new PrivateKey(this._privateKey.bytes);
	}

	/**
	 * Signs a message with the private key.
	 * @param {Uint8Array} message Message to sign.
	 * @returns {Signature} Message signature.
	 */
	sign(message) {
		return new Signature(ml_dsa44.sign(this._keyPair.secretKey, message));
	}
}

/**
 * Verifies signatures signed by a single key pair.
 */
export class Verifier {
	/**
	 * Creates a verifier from a public key.
	 * @param {PublicKey} publicKey Public key.
	 */
	constructor(publicKey) {
		if (0 === deepCompare(new Uint8Array(PublicKey.SIZE), publicKey.bytes))
			throw new Error('public key cannot be zero');

		/**
		 * Public key used for signature verification.
		 * @type {PublicKey}
		 */
		this.publicKey = publicKey;
	}

	/**
	 * Verifies a message signature.
	 * @param {Uint8Array} message Message to verify.
	 * @param {Signature} signature Signature to verify.
	 * @returns {boolean} true if the message signature verifies.
	 */
	verify(message, signature) {
		return ml_dsa44.verify(this.publicKey.bytes, message, signature.bytes);
	}
}
