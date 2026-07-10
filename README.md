# BNL — Post-Quantum Catapult

> **Disclaimer:** This is an **unofficial, independent post-quantum experimental fork** of the Symbol
> monorepo. It is **not affiliated with, endorsed by, or connected to** the official Symbol / NEM
> projects, the NEM Group, or any Symbol (XYM) cryptocurrency, network, or organization. "Symbol",
> "NEM", and related names are used only to describe the upstream software this fork derives from.
> Do **not** treat this project as official Symbol software. Use at your own risk.

> **免責事項:** 本プロジェクトは Symbol モノレポの**非公式かつ独立したポスト量子（PQC）実験フォーク**です。
> 公式 Symbol / NEM プロジェクト、NEM Group、および Symbol（XYM）暗号資産・ネットワーク・関連団体とは
> **一切関係がなく、提携・承認・後援も受けていません**。「Symbol」「NEM」等の名称は、派生元のソフトウェアを
> 説明する目的でのみ使用しています。本プロジェクトを公式 Symbol ソフトウェアとして扱わないでください。
> 利用は自己責任でお願いします。

A blockchain node stack whose **entire cryptographic foundation is post-quantum**: account
signatures, the block lottery (VRF), finalization voting, key exchange, and node-to-node TLS.
This is a **new-chain / hard-fork** change: there is **no backwards compatibility** with the
public Symbol network and a **fresh nemesis block is required**.

## Cryptography

| Purpose | Upstream Symbol | This fork | Standard |
|---|---|---|---|
| Account signatures (tx, blocks, cosignatures) | ed25519 | **ML-DSA-44** | FIPS 204 |
| Block-lottery VRF (generation-hash proof) | ed25519 ECVRF | **iVRF** — hash-based indexed VRF (SHA3-256 Merkle tree) | — |
| Finalization voting | ed25519 (BM tree) | **ML-DSA-44** (BM tree structure retained) | FIPS 204 |
| Delegated-harvesting key exchange / encrypted messages | X25519 ECDH | **ML-KEM-768** | FIPS 203 |
| Node-to-node TLS certificates | ed25519 | **ML-DSA-44** | FIPS 204 |

Sizes: `Key` 32 → **1312 B**, `Signature` 64 → **2420 B**, VRF proof 80 → **1056 B**
(iVRF leaf + authentication path). `PrivateKey` stays **32 B** (all schemes expand
deterministically from the seed). Backed by OpenSSL 3.5+ (native ML-DSA / ML-KEM) — no external
PQC library required in the node. The iVRF security rests on hash assumptions only.

## What changed

- **crypto core** (`client/catapult/src/catapult/crypto`): `Signer` routed to ML-DSA-44 (OpenSSL
  EVP, seed-based deterministic keygen), `SharedKey` → ML-KEM-768, TLS certificates
  (`CertificateUtils`) → ML-DSA-44, new **`iVrf.{h,cpp}`** primitive (configurable-depth SHA3-256
  Merkle tree; `[chain] iVrfTreeDepth`, `iVrfActivationDelay`).
- **consensus**: block headers carry a fixed 1056 B iVRF proof; account state stores the
  registered iVRF root + activation height; `BlockchainProcessor` / `NemesisBlockLoader` verify
  `index = height − activationHeight`; harvesters cache per-account trees. Finalization voting
  (`crypto_voting`) signs with ML-DSA-44 inside the retained Bellare-Miner tree.
- **types / wire format**: `types.h` sizes, `catbuffer` schemas (`PublicKey=1312`,
  `Signature=2420`, `VrfPublicKey=32` — the VRF public key **is** the iVRF tree root),
  `NetworkInfo` gains `NemesisSignerVrfPublicKey`.
- **[@client/rest](client/rest)**: native-crypto ML-DSA key handling (sshpk cannot parse ML-DSA),
  block routes expose `iVrfProofLeaf` / `iVrfProofPath`.
- **[@sdk/javascript](sdk/javascript)**: ML-DSA-44 signing, ML-KEM-768 message encryption, `iVrf`
  module, PQC catbuffer models — byte-compatible with the C++ node. Published standalone as
  [pqc-catapult-sdk-v3](https://github.com/bootarou/pqc-catapult-sdk-v3).
- **tools**: `nemgen` (ML-DSA nemesis + iVRF proof at index 0), `linker`, mongo mappers.
- **Opt-in Chain Finalization**: a `[chain] chainFinalizationHeight` setting (default `0` =
  disabled) that freezes the chain at a target height.

Not PQC-adapted: [@sdk/python](sdk/python) and the NEM side of the JS SDK remain upstream
(ed25519) and are not usable against this chain.

## Verified end-to-end (private network)

Fresh-nemesis PQC network brought up in Docker and validated on live chains: single-node boot +
continuous iVRF/ML-DSA harvesting, 2-node sync (matching block hashes and chain scores), ML-DSA
mutual TLS, transfer / aggregate / multisig-cosignature inclusion via REST `PUT /transactions`,
full stack (node + broker + mongo + REST) serving iVRF proofs over `GET /blocks/*`, C++ ⇄ JS SDK
byte-exact cross-checks on real on-chain blocks, and `symbol-bootstrap` operating the network
with its **unchanged standard CLI** (`config` → `compose` → `up`).

## Documentation

**📘 総括資料 / Work summary: [`PQC-SUMMARY.md`](PQC-SUMMARY.md)**（English: [`PQC-SUMMARY.en.md`](PQC-SUMMARY.en.md)）
— PQC 移行作業の全体像（暗号構成・設計判断・コンポーネント別変更・検証結果・公開物一覧・残課題・索引）。

**📖 起動マニュアル: [`BNL-STARTUP.md`](BNL-STARTUP.md)** — ビルドから nemesis 生成、
フルスタック（node ＋ broker ＋ mongo ＋ REST）起動・検証まで（[`docker-compose.pqc.yml`](docker-compose.pqc.yml) を使用）。

**📄 レポート**: [`PQC-VRF-voting-report.md`](PQC-VRF-voting-report.md) — iVRF・ML-DSA 投票 ／
[`ML-DSA-44-sdk-report.md`](ML-DSA-44-sdk-report.md) — JS SDK ／
[`PQC-bootstrap-report.md`](PQC-bootstrap-report.md) — `symbol-bootstrap` 対応 ／
[`PQC-images.md`](PQC-images.md) — Docker イメージ。

## Build & run

Same toolchain as upstream catapult (see [`client/catapult`](client/catapult)); build inside the
`symbolplatform/symbol-server-build-base` image with OpenSSL 3.5+. Requires a fresh nemesis
(`tools/nemgen`) — the public Symbol network cannot be joined. Prebuilt runtime images are on
Docker Hub (see below), and [`BNL-STARTUP.md`](BNL-STARTUP.md) walks through the full bring-up.

## PQC ecosystem

| Repository / image | Contents |
|---|---|
| **this repo** — [bnl-catapult-pqc](https://github.com/bootarou/bnl-catapult-pqc) | Monorepo: catapult-server, REST gateway, JS SDK (source of truth), catbuffer schemas, docs |
| [pqc-catapult-sdk-v3](https://github.com/bootarou/pqc-catapult-sdk-v3) | Standalone JS SDK v3 — `npm i github:bootarou/pqc-catapult-sdk-v3#feat-pqc` |
| [pqc-catapult-sdk-v2](https://github.com/bootarou/pqc-catapult-sdk-v2) | PQC build of the legacy symbol-sdk 2.x line (used by the explorer) |
| [pqc-catapult-explorer](https://github.com/bootarou/pqc-catapult-explorer) | Block explorer (iVRF proof display) |
| [symbol-bootstrap](https://github.com/bootarou/symbol-bootstrap) (`pqc-bootstrap`) | Network generation / operation CLI with PQC support |
| `nftdrive/bnl-catapult-server-pqc:1.0.3.9-bnl` | Docker: catapult server + broker |
| `nftdrive/bnl-catapult-rest-pqc:2.4.3-bnl` | Docker: REST gateway |

## Upstream

Forked from the [Symbol monorepo](https://github.com/symbol/symbol) (`dev`). The upstream CI /
coverage badges and package links were removed from this README because they describe the
official project, not this fork.
