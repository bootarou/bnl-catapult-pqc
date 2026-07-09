import { sha3_256 } from '@noble/hashes/sha3.js';
import { concatBytes, utf8ToBytes } from '@noble/hashes/utils.js';

/**
 * Post-quantum, hash-based indexed VRF (iVRF) for the block-generation lottery.
 * @note Mirrors the C++ node (crypto/iVrf). Security rests only on SHA3-256.
 *       A harvester derives a fixed-depth Merkle tree of secret leaves from a 32-byte seed and
 *       registers the root on chain; for a block index it reveals the leaf plus its authentication path.
 */

/**
 * Maximum supported iVRF Merkle tree depth (bounds the fixed on-wire proof size).
 * @type {number}
 */
export const IVRF_MAX_TREE_DEPTH = 32;

/**
 * Number of leaves (block indices) covered by a single registration at a given depth.
 * @param {number} depth Tree depth.
 * @returns {bigint} Leaf count.
 */
export const iVrfLeafCount = depth => 1n << BigInt(depth);

const LEAF_LABEL = utf8ToBytes('catapult-ivrf-leaf');

const indexToLe64 = index => {
	const buffer = new Uint8Array(8);
	new DataView(buffer.buffer).setBigUint64(0, BigInt(index), true);
	return buffer;
};

/**
 * Computes the iVRF leaf for an index derived from a seed.
 * @param {Uint8Array} seed 32-byte iVRF seed.
 * @param {number|bigint} index Leaf index.
 * @returns {Uint8Array} 32-byte leaf.
 */
export const computeLeaf = (seed, index) => sha3_256(concatBytes(LEAF_LABEL, seed, indexToLe64(index)));

const hashPair = (left, right) => sha3_256(concatBytes(left, right));

/**
 * In-memory iVRF Merkle tree built from a seed; provides the root and per-index proofs.
 */
export class iVrfKeyTree {
	/**
	 * Builds a tree of the specified depth from a seed.
	 * @param {Uint8Array} seed 32-byte iVRF seed.
	 * @param {number} depth Tree depth (1..IVRF_MAX_TREE_DEPTH).
	 */
	constructor(seed, depth) {
		if (0 === depth || depth > IVRF_MAX_TREE_DEPTH)
			throw new RangeError(`iVrf tree depth ${depth} out of range`);

		const leafCount = Number(iVrfLeafCount(depth));
		const levels = [];

		const leaves = new Array(leafCount);
		for (let i = 0; i < leafCount; ++i)
			leaves[i] = computeLeaf(seed, i);
		levels.push(leaves);

		for (let level = 0; level < depth; ++level) {
			const lower = levels[level];
			const upper = new Array(lower.length / 2);
			for (let i = 0; i < lower.length; i += 2)
				upper[i / 2] = hashPair(lower[i], lower[i + 1]);
			levels.push(upper);
		}

		/**
		 * @private
		 */
		this._depth = depth;

		/**
		 * @private
		 */
		this._levels = levels;
	}

	/**
	 * Gets the tree depth.
	 * @returns {number} Tree depth.
	 */
	get depth() {
		return this._depth;
	}

	/**
	 * Gets the tree root (the registrable public value).
	 * @returns {Uint8Array} 32-byte root.
	 */
	get root() {
		return this._levels[this._depth][0];
	}

	/**
	 * Generates a proof for an index.
	 * @param {number} index Leaf index (must be less than 2^depth).
	 * @returns {{ leaf: Uint8Array, path: Array<Uint8Array> }} Proof (path has depth entries).
	 */
	prove(index) {
		if (BigInt(index) >= iVrfLeafCount(this._depth))
			throw new RangeError(`iVrf index ${index} out of range`);

		const path = new Array(this._depth);
		let nodeIndex = index;
		for (let level = 0; level < this._depth; ++level) {
			path[level] = this._levels[level][nodeIndex ^ 1];
			nodeIndex >>>= 1;
		}

		return { leaf: this._levels[0][index], path };
	}
}

/**
 * Recomputes the Merkle root implied by a proof for an index and compares it to a root.
 * @param {Uint8Array} root 32-byte registered root.
 * @param {number} index Leaf index.
 * @param {{ leaf: Uint8Array, path: Array<Uint8Array> }} proof Proof.
 * @param {number} depth Tree depth used for the registration.
 * @returns {boolean} true if the proof is valid for the root.
 */
export const verify = (root, index, proof, depth) => {
	if (0 === depth || depth > IVRF_MAX_TREE_DEPTH || BigInt(index) >= iVrfLeafCount(depth))
		return false;

	let current = proof.leaf;
	let nodeIndex = index;
	for (let level = 0; level < depth; ++level) {
		current = 0 === (nodeIndex & 1)
			? hashPair(current, proof.path[level])
			: hashPair(proof.path[level], current);
		nodeIndex >>>= 1;
	}

	return Buffer.from(current).equals(Buffer.from(root));
};

/**
 * Computes the iVRF output (block generation hash) binding a proof's leaf to input alpha.
 * @param {{ leaf: Uint8Array, path: Array<Uint8Array> }} proof Proof.
 * @param {Uint8Array} alpha Input (parent generation hash).
 * @returns {Uint8Array} 32-byte generation hash.
 */
export const generationHash = (proof, alpha) => sha3_256(concatBytes(proof.leaf, alpha));
