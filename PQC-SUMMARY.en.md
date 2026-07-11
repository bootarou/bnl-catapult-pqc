# BNL Post-Quantum Catapult — PQC Work Summary

**Scope**: BNL — Post-Quantum Catapult (an unofficial, experimental fork of Symbol/catapult; unaffiliated with official Symbol/NEM)
**Period**: 2026-07-05 — 2026-07-10
**Date**: 2026-07-10 (last updated 2026-07-11)
**Status**: All components implemented, verified on live chains, and published (incl. launcher and Explorer SMD work)

日本語版: [`PQC-SUMMARY.md`](PQC-SUMMARY.md)

---

## 1. Project Overview

We migrated the entire cryptographic foundation of the Symbol/catapult blockchain to
quantum-resistant NIST-standard post-quantum cryptography (PQC). This was not a mere
signature-function swap: the migration covers **every layer — consensus (block lottery and
finality voting), key exchange, TLS, wire format, state, REST, SDK, and operational
tooling** — and the result was verified end to end on live chains as a private network
bootstrapped from a fresh nemesis.

### Final cryptographic profile

| Purpose | Old (official Symbol) | New (BNL PQC) | Standard | Rationale / notes |
|---|---|---|---|---|
| Account signatures (tx / block) | ed25519 | **ML-DSA-44** | FIPS 204 | Native in OpenSSL 3.5+. Private keys remain 32-byte seeds |
| Block-lottery VRF | ECVRF (edwards25519) | **iVRF** (hash-based indexed VRF, SHA3-256 Merkle tree) | — (AsiaCCS 2023 / eprint 2022/993) | No standardized PQ-VRF exists; security rests on hash assumptions only |
| Finality voting | ed25519 (inside BM two-layer tree) | **ML-DSA-44** (BM tree structure retained = forward security preserved) | FIPS 204 | |
| Key exchange (delegated harvesting / encrypted messages) | X25519 ECDH | **ML-KEM-768** | FIPS 203 | KEM shape requires shipping a 1088 B ciphertext alongside messages |
| Node-to-node TLS | ed25519 certificates | **ML-DSA-44 certificates** | FIPS 204 | TLS 1.3 handshake ECDHE (X25519) is independent of certificate signatures and is retained |

### Size impact (the source of every difficulty in this migration)

| Item | ed25519 | ML-DSA-44 / iVRF | Delta |
|---|---|---|---|
| Private key | 32 B | 32 B (seed) | ±0 |
| Public key `Key` | 32 B | **1312 B** | +1280 B |
| Signature `Signature` | 64 B | **2420 B** | +2356 B |
| Tx header (signer key + signature) | 96 B | 3732 B | +3636 B / tx |
| VRF proof (block header) | 80 B (ECVRF) | **1056 B** (iVRF leaf 32 B + fixed 32×32 B path) | +976 B / block |
| VotingKey / VotingSignature | 32 B / 64 B | 1312 B / 2420 B | voting proofs ≈ 38× |

Because key and signature sizes change by orders of magnitude, compatibility with existing
chains is impossible; the design assumes a **new network bootstrapped from a fresh nemesis**
(protocol-incompatible with the public Symbol network).

---

## 2. Timeline

| Date | Phase | Outcome |
|---|---|---|
| 07-05 | Environment | Fork checkout, build-base image (gcc-15 / OpenSSL 3.6.2), BNL image build scripts |
| 07-08 | Research & design | Migration playbook (`ML-DSA-44-migration.md`), FN-DSA comparison, interim PQ-VRF survey |
| 07-08 | Core migration & bring-up | C++ crypto core to ML-DSA, fresh nemesis, **two live nodes syncing to identical block hashes** |
| 07-08 | REST ingestion path | ML-DSA tx announced via REST `PUT /transactions` → **confirmed in a block** |
| 07-09 | SDK / bootstrap groundwork | JS SDK ML-DSA signing / address derivation / serialization verified byte-identical to C++; bootstrap plan fixed |
| 07-09–10 | Consensus PQC | Voting to ML-DSA, iVRF primitive → consensus integration → **two-node live-chain verification** |
| 07-10 | Full stack & operations | docker-compose end-to-end verification, symbol-bootstrap standard-CLI support, SDK ML-KEM message encryption |
| 07-10 | Publication | 2 Docker Hub images, 4 GitHub repositories (catapult / SDK / explorer / bootstrap) |
| 07-11 | Launcher | BNL (blockchain-network-launcher) reworked into a **PQC-only launcher**; 8-item acceptance run passed. **First live-chain verification of ML-DSA voting keys** |
| 07-11 | Explorer SMD | Merged the SMD feature set (explorer-smd `main`) into the PQC explorer; removed official logos/favicons; fixed SPA/REST routing |

---

## 3. Changes by Component

### 3-1. catapult-server (C++ / `client/catapult`)

**Crypto core (Phase 1)**
- `types.h`: `Key` 32→1312, `Signature` 64→2420, `VotingKey` 32→1312. VRF keys split off as a
  distinct `VrfPublicKey` (32 B) type
- `crypto/Signer.cpp`: donna ed25519 → OpenSSL EVP (`EVP_PKEY_ML_DSA_44`); ed25519-specific
  S-part canonicality checks removed
- `crypto/KeyPair.cpp`: 32 B seed → deterministic key expansion via `OSSL_PKEY_PARAM_ML_DSA_SEED`
  (preserving private-key format and backup workflows)
- `crypto/OpensslKeyUtils.cpp`: `get_raw_*_key` does not support ML-DSA → rewritten to
  octet-string / seed params
- Batch signature verification (`VerifyMulti`): donna batch verify → plain loop (ML-DSA has no
  batch verification)
- `crypto/SharedKey.cpp`: X25519 ECDH → **ML-KEM-768** encapsulation (delegated-harvesting
  decryption path moved to `MlKemKeyPair`)

**iVRF (block lottery)**
- New `crypto/iVrf.{h,cpp}`: configurable-depth (default 2^16) SHA3-256 Merkle tree.
  `leaf(i) = SHA3-256("catapult-ivrf-leaf" || seed || i_le64)`, proof = leaf + authentication
  path, generation hash = `SHA3-256(leaf_i || parentGenerationHash)`. gtest suite included
  (tamper / wrong-index / out-of-range / wrong-seed rejection, determinism)
- Block header: `GenerationHashProof` (ECVRF, 80 B) → `iVrfProof` (fixed 1056 B; header layout
  is invariant under config depth changes)
- Account state: VRF supplemental now stores **root + activation height**; an observer records
  the activation height (link height + `iVrfActivationDelay`) when a VrfKeyLink confirms
  (anti-grinding for targeted root registration)
- Verification: `BlockchainProcessor` / `NemesisBlockLoader` check root, validity window, and
  `index = height − activationHeight`
- Harvester: per-account tree cache (~290 ms / 4 MB to build), then `prove(index)`
- `KeyPair.cpp`: **VRF public key derivation = root of the iVRF tree built from the seed**
  (`a5c1c36`; the key to bootstrap compatibility)
- Config: added `[chain] iVrfTreeDepth` (default 16) / `iVrfActivationDelay` (default 0)

**Finality voting**
- `crypto_voting`: `VotingSigner` / `VotingKeyPair` delegate to ML-DSA (`5717ca868`).
  The Bellare-Miner two-layer tree (epoch/step forward security) is retained.
  `BmPrivateKeyTree` / `FinalizationMessage` track sizes automatically via `::Size`

**TLS / tools**
- `CertificateUtils.cpp`: hardcoded `EVP_PKEY_ED25519` → ML-DSA-44 check; node identity =
  1312 B ML-DSA public key
- `tools/nemgen`: ML-DSA-signed nemesis + iVRF proof (index 0), new
  `nemesisSignerVrfPrivateKey`, logs the nemesis iVRF root
- `tools/linker` / `AccountPrinter` / mongo `AccountStateMapper` (templated over
  `Key`/`VrfPublicKey`) and friends updated accordingly

### 3-2. REST gateway (`client/rest`)

- `src/index.js`: `sshpk` cannot parse ML-DSA keys (OID 2.16.840.1.101.3.4.3.17), so REST failed
  to start → replaced with Node's built-in `crypto` (OpenSSL 3.5+) extracting the public key
  from SPKI DER
- Block routes/schema: expose the iVRF proof (`iVrfProofLeaf` + `iVrfProofPath`); legacy
  `proofGamma` fields removed
- Uses the PQC `symbol-sdk` (local → now GitHub `pqc-catapult-sdk-v2#feat-pqc`)
- Key design finding: `PUT /transactions` forwards the payload **opaquely**, so no catbuffer
  size changes are needed on the announce path (only GET-side deserialization needs them)
- Operations: REST requires a **certificate identity separate from the API node** (reusing the
  node's certificate gets rejected as a duplicate identity)

### 3-3. JavaScript SDK v3 (`sdk/javascript` → published as `pqc-catapult-sdk-v3`)

- `CryptoTypes.js` / `models.js` (catbuffer): PublicKey 1312 / Signature 2420 / new
  `VrfPublicKey` (32 B) type / `VotingPublicKey` 1312. Block model carries the iVRF proof
  (leaf + path, fixed 1056 B)
- `KeyPair.js`: `ml_dsa44` from `@noble/post-quantum` (**pinned to 0.4.1**; 0.5.x reverses the
  `sign` argument order)
- `SharedKey.js` / `MessageEncoder.js`: fully rewritten on **ML-KEM-768**.
  Seed derivation `SHA512("catapult-mlkem-seed" || privateKey)[:64]`, HKDF-SHA256,
  message format `[marker][cipherText 1088][tag][iv][AES-GCM ciphertext]` (interoperable with
  the C++ node)
- `symbol/iVrf.js`: iVRF implementation identical to C++ (root/leaf/path/generationHash match
  **byte for byte**)
- `VotingKeysGenerator`: sizes computed from key/signature constants (byte-identical layout to
  C++ `BmPrivateKeyTree`)
- `utils/converter.js`: 2420 B signatures break 8-byte alignment → unaligned-read fallback added
- Standalone publication: extracted into `pqc-catapult-sdk-v3` via `git subtree split` (history
  preserved) and `iVrf` exported from the `symbol-sdk/symbol` entry point. Pure ESM with no
  build step, so `npm i github:bootarou/pqc-catapult-sdk-v3#feat-pqc` just works

**Legacy SDK v2** (`pqc-catapult-sdk-v2`, used by the explorer): the separate TypeScript SDK
2.0.6 line was also PQC-enabled. For npm git installs, broken submodules
(`catbuffer-generators` / `travis`, upstream repos gone) were removed and
`prepare: npm run build` added, so `npm i github:bootarou/pqc-catapult-sdk-v2#feat-pqc` works

### 3-4. symbol-bootstrap (published: `bootarou/symbol-bootstrap`, branch `pqc-bootstrap`)

PQC networks can now be generated and operated **without changing how the tool is used**
(the standard `config` → `compose` → `up` CLI):
- `CertificateService`: deterministic key/certificate generation via
  `openssl genpkey -algorithm ML-DSA-44 -pkeyopt hexseed:<seed>`
- Key generation: ML-DSA account derivation (public keys and addresses match catapult), VRF as
  iVRF root, voting as ML-DSA
- `ComposeService`: mongo data directory moved to `/data/db` (rides the official entrypoint's
  chown path, fixing permissions)
- `rest.json.mustache`: added `routeExtensions: []` (required by the PQC REST 2.5.1 line)
- `presets/shared.yml`: references the PQC images (`nftdrive/bnl-catapult-*-pqc`)

**Added for the launcher work (2026-07-11)**: rewrote `VotingUtils` for **ML-DSA-44 voting key
trees** (root public key 1312 B / signatures 2420 B, header 48+1312 — the classic ed25519 size
check rejected the PQC `catapult.tools.votingkey` output of 1,766,800 B; voting-enabled nodes
were the one previously untested path). Also declared phantom dependencies (`tweetnacl` /
`symbol-openapi-typescript-fetch-client`) and restored the executable bit on `bin/run`.

### 3-5. Explorer (published: `pqc-catapult-explorer`, branch `feat-pqc`)

- Displays iVRF block proofs (`proofGamma` fields removed in favour of
  `iVrfProofLeaf`/`iVrfProofPath`); uses the PQC symbol-sdk v2
  (`github:bootarou/pqc-catapult-sdk-v2#feat-pqc`)
- **SMD integration (2026-07-11)**: merged the SMD feature set from explorer-smd `main`
  (social metadata, SMD list page, mosaic holder list / transaction history). `feat-pqc` is now
  the **NFTDrive SMD edition + PQC** combined build (plan & verification: `PQC-SMD-PLAN.md`
  in the repo)
- **Trademark cleanup**: official Symbol logos (header wordmark, 3 menu marks) replaced with a
  "BNL PQC" text brand; the official favicon set removed. Footer links only the BNL repo
- The SMD transaction lists were the first code path to render nemesis key-link transactions,
  exposing a hardcoded 64-hex `linkedPublicKey` validation in SDK v2 → **fixed to accept
  2624-hex ML-DSA keys** (Account/Node/Voting links; VRF stays 64 hex for the 32 B root)

### 3-6. Supporting tools (outside the repo, `/home/user/catapult/`)

| Tool | Purpose |
|---|---|
| `poc/` | C++ standalone PoCs (key generation `netgen_keys`, tx signing `txsign`, p2p push `txpush`, ML-DSA verification, …) |
| `jssign/` | Pure-JS transaction builders (`txbuild` / `vrflink` / `votinglink` / `addr`, ML-DSA signing) |
| `net/` | Experimental network assets (keys, ML-DSA certificates, configs, nemesis seed, per-node data) |
| `build-target.sh` / `build-bnl-image.sh` | Individual-target / release-image builds |
| `docker-compose.pqc.yml` | Full-stack bring-up: mongo + node-api + broker + REST (also bundled in this repo) |

### 3-7. BNL launcher (blockchain-network-launcher, branch `feat-PQC-custom-catapult`)

A **PQC-only launcher** for building and operating PQC networks from a Web UI (deliberately
PQC-only rather than a classic/PQC toggle; rationale, plan and acceptance record live in the
launcher repo's `docs/PQC-LAUNCHER-PLAN.md`). **This branch is never merged into main/dev.**

- **pqc-bootstrap baked into the image**: git clone + `npm install --omit=dev` + bin symlink
  instead of the unreliable `npm install -g <git-url>` (npm 10.8 symlink bug, `files`
  whitelist, prepack). Build-time sanity checks assert the ML-DSA edition; at runtime the
  npx-cache / registry fallbacks are removed so a classic ed25519 bootstrap can never run silently
- **Single built-in catapult version `pqc`** (`nftdrive/bnl-catapult-*-pqc` images);
  `chainFinalizationHeight` promoted to a Configuration-UI setting
- **PQC-only UI**: joining official mainnet/testnet is refused at import time, 2624-hex public
  keys are truncated for display, rebranded as "BNL Post-Quantum Network Manager"
- **Explorer integration**: builds and runs `pqc-catapult-explorer#feat-pqc`. The generated
  image/proxy gained ① branch-tip cache-busting, ② a multi-stage build (deps unchanged →
  `npm install` cached; no-change rebuilds ~5 s, source-only ~2–3 min), ③ **Accept-header
  SPA/REST routing** (resolves the history-mode SPA vs REST path collision; also cherry-picked
  to the classic `main`/`feat-custom-catapult` branches), ④ no-cache headers for HTML/`/config`
- **Acceptance run (8/8 passed)**: network creation via the UI API → iVRF/ML-DSA harvesting →
  exact freeze at `chainFinalizationHeight=15` → stop/resume → backup → Explorer browsing.
  **First live-chain verification of a voting-enabled node (ML-DSA voting key generation +
  nemesis link)**

---

## 4. Verification Summary

| # | Verification | Result | Decisive evidence |
|---|---|---|---|
| 1 | Node boots from ML-DSA nemesis and harvests continuously | ✅ | `loaded blockchain (height=1)` → continuous harvest |
| 2 | Two-node sync (ML-DSA) | ✅ | Identical block hashes (h5:`927B4078`, …), bidirectional Push_Block |
| 3 | ML-DSA tx via REST → confirmed in a block | ✅ | `PUT /transactions` 202 → confirmed at height 367 (non-empty 7 KB block) |
| 4 | Mutual TLS with ML-DSA certificates | ✅ | Established peer-to-peer and REST (Node 22) ⇄ node |
| 5 | Voting (ML-DSA BM tree) library verification | ✅ | Sign/verify, tamper rejection, serialization round-trip PASS; SDK-generated key tree byte-identical to C++ |
| 6 | iVRF two-node live chain | ✅ | Nemesis iVRF root loads verified → continuous harvest → node-b verifies proofs, chain scores match, zero errors |
| 7 | C++ ⇄ JS SDK iVRF cross-check | ✅ | Real on-chain block deserialize→re-serialize byte-exact; leaf matches SDK `computeLeaf` |
| 8 | ML-KEM message-encryption interop | ✅ | noble ⇄ OpenSSL shared secrets match, AES-GCM tamper detection, delegation round-trip |
| 9 | Full stack (node+broker+mongo+REST) | ✅ | `GET /blocks/2` returns the iVRF proof; `/chain/info` height advances |
| 10 | PQC network via standard symbol-bootstrap commands | ✅ | `config -a dual` → `compose` → `up` mines past height 155, zero errors |
| 11 | BNL launcher acceptance (8 items) | ✅ | UI-driven network creation / iVRF harvesting / freeze at height 15 (4×30 s checks) / resume / backup with ML-DSA keys |
| 12 | SMD Explorer on a live chain | ✅ | Holder list returns 2624-hex ML-DSA public keys, iVRF proofs shown, key-link transaction lists render (after the SDK fix) |

---

## 5. Published Artifacts (all public; verified 2026-07-10)

### GitHub (account: `bootarou`)

| Repository | Branch | Contents |
|---|---|---|
| [bnl-catapult-pqc](https://github.com/bootarou/bnl-catapult-pqc) | `feat-VRF/votiong` | Main monorepo (catapult-server / REST / SDK / docs) |
| [pqc-catapult-sdk-v3](https://github.com/bootarou/pqc-catapult-sdk-v3) | `feat-pqc` | **JavaScript SDK v3** (extracted from the monorepo's `sdk/javascript` with full history; pure ESM, no build step, installable via `npm i github:bootarou/pqc-catapult-sdk-v3#feat-pqc`) |
| [pqc-catapult-sdk-v2](https://github.com/bootarou/pqc-catapult-sdk-v2) | `feat-pqc` | TypeScript SDK v2 (PQC build of the legacy symbol-sdk 2.x line, used by the explorer; usable as an npm git dependency) |
| [pqc-catapult-explorer](https://github.com/bootarou/pqc-catapult-explorer) | `feat-pqc` | Block explorer (**NFTDrive SMD edition + PQC combined**; iVRF proof display, official trademarks removed) |
| [symbol-bootstrap](https://github.com/bootarou/symbol-bootstrap) | `pqc-bootstrap` | PQC support for the network-generation/operation CLI |
| [blockchain-network-launcher](https://github.com/bootarou/blockchain-network-launcher) | `feat-PQC-custom-catapult` | **PQC-only edition of the BNL launcher** (Web UI; independent branch, never merged into main) |

### Docker Hub (namespace: `nftdrive`, linux/amd64)

| Image | Digest (Hub = local, verified) | Role |
|---|---|---|
| `nftdrive/bnl-catapult-server-pqc:1.0.3.9-bnl` | `sha256:620b960636ed…` | catapult server + broker (3.95 GB) |
| `nftdrive/bnl-catapult-rest-pqc:2.4.3-bnl` | `sha256:570ee2e29bc3…` | REST gateway (726 MB) |

mongo uses the unmodified official `mongo:5.0.15` (not pushed). Details: [`PQC-images.md`](PQC-images.md).

---

## 6. Key Design Decisions and Rationale

1. **Choosing ML-DSA-44** (vs FN-DSA-512): FIPS 204 is final, OpenSSL support is native, and
   implementation risk is low. FN-DSA is ~2.5× smaller but FIPS 206 is unfinalized, OpenSSL
   support is absent, and its floating-point signing carries implementation risk — deferred as a
   second-generation network option (details: `FN-DSA-migration-diff.md`)
2. **Adopting iVRF** (vs keeping ECVRF / NIS1-style public hash / LaV): with no standardized
   PQ-VRF, iVRF is the only practical construction offering uniqueness, secrecy, and
   verifiability from hash assumptions alone. Its ~1 KB proof is far smaller than LaV (12 KB).
   Height windows + re-registration + activation delay prevent grinding (details:
   `VRF-PQ-interim-options.md`). X-VRF (XMSS-based) was excluded outright — its uniqueness was
   broken (FC'24)
3. **Keeping 32-byte-seed private keys**: preserves key backup, BIP32 derivation, configuration
   file formats, and bootstrap key management wholesale (ML-DSA, ML-KEM, and iVRF are all
   derived deterministically from the seed)
4. **VRF public key = iVRF tree root**: reuses the existing VrfKeyLink transaction and the 32 B
   AccountPublicKeys schema unmodified, so bootstrap's key-link generation keeps working as-is
5. **Fixed-size proof field** (reserving depth 32 = 1056 B): the block-header layout stays
   invariant under config depth changes, and leaves room to swap in LaV or similar later
6. **Accepting the fresh-nemesis constraint**: every hash and address changes, so compatibility
   with existing chains is fundamentally impossible. Explicitly scoped to private/experimental
   networks like BNL (the README carries the unofficial-fork disclaimer)

---

## 7. Known Limitations / Remaining Work

| # | Item | Status |
|---|---|---|
| 1 | crypto gtests / SDK test vectors still assume ed25519 | Not updated (runtime covered by e2e). Proper fix: switch to NIST ACVP vectors |
| 2 | Live-chain observation of BFT finality **epoch progression** | Not done (ML-DSA voting key generation, nemesis link and harvesting on a voting-enabled node were verified live via the launcher; the test chain froze at chainFinalizationHeight before reaching epoch 2, so only the progression itself remains unobserved) |
| 3 | State-hash equality with `enableVerifiableState=true` | Not done (block-hash equality confirmed) |
| 4 | Operational test of re-registration at iVRF window expiry (2^16 blocks) | Not done (mechanism implemented) |
| 5 | Distribution channel for ML-KEM public keys (1184 B) | Not derivable from the account public key; on-chain publication mechanism not designed |
| 6 | Image-size reduction (server 3.95 GB), arm64 multi-arch | Not started (`PQC-images.md` §5) |
| 7 | Default `iVrfActivationDelay` for grinding resistance | 0 (disabled). Production deployments should set a delay comparable to importance grouping |

---

## 8. Document Index

### In this repository (`symbol/`)
| Document | Contents |
|---|---|
| [`README.md`](README.md) | Project overview and disclaimer |
| [`BNL-STARTUP.md`](BNL-STARTUP.md) | Hands-on manual: build → nemesis → full-stack bring-up, verification, troubleshooting |
| [`PQC-VRF-voting-report.md`](PQC-VRF-voting-report.md) | Details of ML-DSA voting and iVRF implementation/integration/live-chain verification |
| [`ML-DSA-44-sdk-report.md`](ML-DSA-44-sdk-report.md) | JS SDK (signing, catbuffer, ML-KEM message encryption) |
| [`PQC-bootstrap-report.md`](PQC-bootstrap-report.md) | symbol-bootstrap support and verification |
| [`PQC-images.md`](PQC-images.md) | Docker image inventory and publication status |

### Working directory (`/home/user/catapult/`, research/work records outside the repo; Japanese)
| Document | Contents |
|---|---|
| `ML-DSA-44-migration.md` | Full-stack migration playbook (Phases 0–8; the blueprint for this work) |
| `ML-DSA-44-bringup-report.md` | First live-chain bring-up and two-node sync after the core migration |
| `ML-DSA-44-rest-report.md` | Proof that REST-announced transactions get confirmed in blocks |
| `ML-DSA-44-bootstrap-report.md` | Groundwork and proven code for the bootstrap ML-DSA support |
| `VRF-PQ-interim-options.md` | Survey of interim PQ options for the VRF (basis for adopting iVRF) |
| `FN-DSA-migration-diff.md` | ML-DSA-44 vs FN-DSA-512 differential study (basis for the algorithm choice) |

### Plan / verification records in other repositories
| Document | Contents |
|---|---|
| [launcher `docs/PQC-LAUNCHER-PLAN.md`](https://github.com/bootarou/blockchain-network-launcher/blob/feat-PQC-custom-catapult/docs/PQC-LAUNCHER-PLAN.md) | PQC-only launcher plan + acceptance results |
| [explorer `PQC-SMD-PLAN.md`](https://github.com/bootarou/pqc-catapult-explorer/blob/feat-pqc/PQC-SMD-PLAN.md) | SMD integration plan + verification results |
