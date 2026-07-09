import { KeyPair } from './KeyPair.js';
import { PrivateKey, PublicKey, Signature } from '../CryptoTypes.js';

const setBuffer = (destination, offset, source) => {
	source.forEach((byte, i) => { destination.setUint8(offset + i, source[i]); });
};

/**
 * Generates symbol voting keys.
 */
export default class VotingKeysGenerator {
	/**
	 * Creates a generator around a voting root key pair.
	 * @param {KeyPair} rootKeyPair Voting root key pair.
	 * @param {Function} privateKeyGenerator Private key generator.
	 */
	constructor(rootKeyPair, privateKeyGenerator = PrivateKey.random) {
		/**
		 * @private
		 */
		this._rootKeyPair = rootKeyPair;

		/**
		 * @private
		 */
		this._privateKeyGenerator = privateKeyGenerator;
	}

	/**
	 * Generates voting keys for specified epochs.
	 * @param {bigint} startEpoch Start epoch.
	 * @param {bigint} endEpoch End epoch.
	 * @returns {Uint8Array} Serialized voting keys.
	 */
	generate(startEpoch, endEpoch) {
		// layout (matches C++ crypto_voting/BmPrivateKeyTree): tree header (4 x uint64) +
		// level header (root public key + 2 x uint64) + one entry per epoch (child private key + signature)
		// note: sizes scale with the ML-DSA-44 voting key (PublicKey 1312, Signature 2420)
		const LEVEL_HEADER_OFFSET = 32; // after start/end/last/lastWiped key identifiers
		const HEADER_SIZE = LEVEL_HEADER_OFFSET + PublicKey.SIZE + 16;
		const EPOCH_ENTRY_SIZE = PrivateKey.SIZE + Signature.SIZE;

		const numEpochs = Number(endEpoch - startEpoch + 1n);
		const buffer = new ArrayBuffer(HEADER_SIZE + (EPOCH_ENTRY_SIZE * numEpochs));

		const view = new DataView(buffer);
		view.setBigUint64(0, startEpoch, true); // start key identifier
		view.setBigUint64(8, endEpoch, true); // end key identifier
		view.setBigUint64(16, 0xFFFFFFFFFFFFFFFFn, true); // reserved - last (used) key identifier
		view.setBigUint64(24, 0xFFFFFFFFFFFFFFFFn, true); // reserved - last wiped key identifier

		setBuffer(view, LEVEL_HEADER_OFFSET, this._rootKeyPair.publicKey.bytes); // root voting public key
		view.setBigUint64(LEVEL_HEADER_OFFSET + PublicKey.SIZE, startEpoch, true); // level 1/1 start key identifier
		view.setBigUint64(LEVEL_HEADER_OFFSET + PublicKey.SIZE + 8, endEpoch, true); // level 1/1 end key identifier

		for (let i = 0; i < numEpochs; ++i) {
			const identifier = endEpoch - BigInt(i);
			const childPrivateKey = this._privateKeyGenerator();
			const childKeyPair = new KeyPair(childPrivateKey);

			const parentSignedPayloadBuffer = new ArrayBuffer(PublicKey.SIZE + 8);
			const parentSignedPayloadView = new DataView(parentSignedPayloadBuffer);
			setBuffer(parentSignedPayloadView, 0, childKeyPair.publicKey.bytes);
			parentSignedPayloadView.setBigUint64(PublicKey.SIZE, identifier, true);
			const signature = this._rootKeyPair.sign(new Uint8Array(parentSignedPayloadBuffer));

			const startOffset = HEADER_SIZE + (EPOCH_ENTRY_SIZE * i);
			setBuffer(view, startOffset, childKeyPair.privateKey.bytes); // child voting private key used to sign votes for an epoch
			setBuffer(view, startOffset + PrivateKey.SIZE, signature.bytes); // signature proving derivation of child key pair from root
		}

		return new Uint8Array(buffer);
	}
}
