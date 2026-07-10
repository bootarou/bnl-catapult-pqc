# BNL — Post-Quantum Catapult (ML-DSA-44)

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

> This is a **post-quantum fork** of the Symbol monorepo. The account signature scheme has been
> migrated from ed25519 to **ML-DSA-44 (FIPS 204)**, with **ML-KEM-768 (FIPS 203)** for delegated-harvesting
> key exchange. The VRF (block lottery) and finalization voting remain on ed25519 (hybrid design).
> This is a **new-chain / hard-fork** change: there is **no backwards compatibility** with the public
> Symbol network and a **new nemesis block is required**.

## Cryptography

| Purpose | Original | This fork |
|---|---|---|
| Account signatures (tx, blocks, cosignatures, TLS certs) | ed25519 | **ML-DSA-44** (FIPS 204) |
| Delegated-harvesting key exchange | X25519 ECDH | **ML-KEM-768** (FIPS 203) |
| VRF (block generation lottery) | ed25519 ECVRF | **ed25519 ECVRF (retained)** |
| Finalization voting | ed25519 | **ed25519 (retained)** |

Sizes: `Key` 32 → **1312 B**, `Signature` 64 → **2420 B**. `PrivateKey` stays **32 B** (ML-DSA seed;
key expansion is deterministic). A separate `VrfPublicKey` (32 B) type is introduced for the retained
ed25519 VRF path. Backed by OpenSSL 3.5+ (native ML-DSA / ML-KEM); no external PQC library required.

## What changed

- **crypto core** (`client/catapult/src/catapult/crypto`): `MlDsa.{h,cpp}` (OpenSSL EVP, deterministic
  signing), `Signer` routed to ML-DSA, ed25519 isolated into `Ed25519Signer.{h,cpp}` for VRF/voting,
  `SharedKey` → ML-KEM-768, `OpensslKeyUtils` reads ML-DSA PEM keys via octet/seed params, TLS
  certificates (`CertificateUtils`) switched to ML-DSA-44.
- **types / wire format**: `types.h` sizes, `catbuffer` schemas (`PublicKey=1312`, `Signature=2420`,
  new `VrfPublicKey=32`), `NetworkInfo` gains `NemesisSignerVrfPublicKey`.
- **VRF path** retyped to `VrfKeyPair`/`VrfPublicKey` end-to-end (account state, key-link, harvesting,
  block processing) while keeping the ECVRF algorithm unchanged.
- **tools**: `nemgen` (ML-DSA nemesis + `nemesisSignerVrfPrivateKey`), `linker` (VrfKeyLink/VotingKeyLink,
  live network-time deadline), mongo mappers, and `client/rest` (native-crypto ML-DSA key handling).
- **Opt-in Chain Finalization**: a `[chain] chainFinalizationHeight` setting (default `0` = disabled)
  that freezes the chain at a target height (stops harvesting and rejects higher blocks).

## Verified end-to-end (private network)

New-nemesis ML-DSA network brought up in Docker and validated: single-node boot + block generation,
2-node sync (matching block hashes), ML-DSA TLS peer handshakes, transfer / **Aggregate Complete** /
**multisig cosignature** (1312 B key + 2420 B sig) inclusion via **REST `PUT /transactions`**, balance
changes queryable via REST, **BFT finalization** advancing across nodes (ed25519 voting), and a
pure-JS transaction signer (`@noble/post-quantum` ml-dsa44, cross-verified against OpenSSL).

## Build & run

Same toolchain as upstream catapult (see [`client/catapult`](client/catapult)); build inside the
`symbolplatform/symbol-server-build-base` image with OpenSSL 3.5+. Requires a fresh nemesis
(`tools/nemgen`) — the public Symbol network cannot be joined.

**📘 総括資料: [`PQC-SUMMARY.md`](PQC-SUMMARY.md)** — PQC 移行作業の全体像（暗号構成・設計判断・
コンポーネント別変更・検証結果・公開物一覧・残課題・ドキュメント索引）。

**📖 起動マニュアル: [`BNL-STARTUP.md`](BNL-STARTUP.md)** — ビルドから nemesis 生成、
フルスタック（node ＋ broker ＋ mongo ＋ REST）起動・検証まで（[`docker-compose.pqc.yml`](docker-compose.pqc.yml) を使用）。

**📄 レポート**: [`PQC-bootstrap-report.md`](PQC-bootstrap-report.md) — `symbol-bootstrap` の標準コマンドで PQC チェーンを運用する対応と検証結果 ／ [`PQC-VRF-voting-report.md`](PQC-VRF-voting-report.md) — iVRF・ML-DSA 投票 ／ [`ML-DSA-44-sdk-report.md`](ML-DSA-44-sdk-report.md) — JS SDK。

---

# Symbol Monorepo

In Q1 2021, we consolidated a number of projects into this repository.
It includes our specialized binary payload DSL (parser and schemas), clients and sdks.

| component | lint | build | test | coverage | package |
|-----------|------|-------|------|----------| ------- |
| [@catbuffer/parser](catbuffer/parser) | [![lint][catbuffer-parser-lint]][catbuffer-job] || [![test][catbuffer-parser-test]][catbuffer-job] <br> [![vectors][catbuffer-parser-vectors]][catbuffer-job] | [![][catbuffer-parser-cov]][catbuffer-parser-cov-link] | [![][catbuffer-package]][catbuffer-package-link] |
|||||||
| [@client/catapult](client/catapult) | [![lint][client-catapult-lint]][client-catapult-job] | [![build][client-catapult-build]][client-catapult-job] | [![build][client-catapult-test]][client-catapult-job] | [![][client-catapult-cov]][client-catapult-cov-link] |
| [@client/rest](client/rest) | [![lint][client-rest-lint]][client-rest-job] || [![test][client-rest-test]][client-rest-job] | [![][client-rest-cov]][client-rest-cov-link] |
|||||||
| [@sdk/javascript](sdk/javascript) | [![lint][sdk-javascript-lint]][sdk-javascript-job] | [![build][sdk-javascript-build]][sdk-javascript-job] | [![test][sdk-javascript-test]][sdk-javascript-job] <br> [![examples][sdk-javascript-examples]][sdk-javascript-job] <br> [![vectors][sdk-javascript-vectors]][sdk-javascript-job] | [![][sdk-javascript-cov]][sdk-javascript-cov-link] | [![][sdk-javascript-package]][sdk-javascript-package-link] |
| [@sdk/python](sdk/python) | [![lint][sdk-python-lint]][sdk-python-job] | [![build][sdk-python-build]][sdk-python-job] | [![test][sdk-python-test]][sdk-python-job] <br> [![examples][sdk-python-examples]][sdk-python-job] <br> [![vectors][sdk-python-vectors]][sdk-python-job] | [![][sdk-python-cov]][sdk-python-cov-link] | [![][sdk-python-package]][sdk-python-package-link] |
|||||||
| [@linters](linters) | [![lint][linters-lint]][linters-job] |||||
| [@jenkins](jenkins) | [![lint][jenkins-lint]][jenkins-job] |||||

## Full Coverage Report

Detailed version can be seen on [codecov.io][symbol-cov-link].

[![][symbol-cov]][symbol-cov-link]

[symbol-cov]: https://codecov.io/gh/symbol/symbol/branch/dev/graphs/tree.svg
[symbol-cov-link]: https://codecov.io/gh/symbol/symbol/tree/dev

[catbuffer-job]: https://jenkins.symbolsyndicate.us/blue/organizations/jenkins/Symbol%2Fgenerated%2Fsymbol%2Fcatbuffer-parser/activity/?branch=dev
[catbuffer-parser-lint]: https://jenkins.symbolsyndicate.us/buildStatus/icon?job=Symbol%2Fgenerated%2Fsymbol%2Fcatbuffer-parser%2Fdev%2F&config=catbuffer-parser-lint
[catbuffer-parser-test]: https://jenkins.symbolsyndicate.us/buildStatus/icon?job=Symbol%2Fgenerated%2Fsymbol%2Fcatbuffer-parser%2Fdev%2F&config=catbuffer-parser-test
[catbuffer-parser-vectors]: https://jenkins.symbolsyndicate.us/buildStatus/icon?job=Symbol%2Fgenerated%2Fsymbol%2Fcatbuffer-parser%2Fdev%2F&config=catbuffer-parser-vectors
[catbuffer-parser-cov]: https://codecov.io/gh/symbol/symbol/branch/dev/graph/badge.svg?token=SSYYBMK0M7&flag=catbuffer-parser
[catbuffer-parser-cov-link]: https://codecov.io/gh/symbol/symbol/tree/dev/catbuffer/parser
[catbuffer-package]: https://img.shields.io/pypi/v/catparser
[catbuffer-package-link]: https://pypi.org/project/catparser

[client-catapult-job]: https://jenkins.symbolsyndicate.us/blue/organizations/jenkins/Symbol%2Fgenerated%2Fsymbol%2Fclient-catapult/activity?branch=dev
[client-catapult-lint]: https://jenkins.symbolsyndicate.us/buildStatus/icon?job=Symbol%2Fgenerated%2Fsymbol%2Fclient-catapult%2Fdev%2F&config=client-catapult-lint
[client-catapult-build]: https://jenkins.symbolsyndicate.us/buildStatus/icon?job=Symbol%2Fgenerated%2Fsymbol%2Fclient-catapult%2Fdev%2F&config=client-catapult-build
[client-catapult-test]: https://jenkins.symbolsyndicate.us/buildStatus/icon?job=Symbol%2Fgenerated%2Fsymbol%2Fclient-catapult%2Fdev%2F&config=client-catapult-test
[client-catapult-cov]: https://codecov.io/gh/symbol/symbol/branch/dev/graph/badge.svg?token=SSYYBMK0M7&flag=client-catapult
[client-catapult-cov-link]: https://codecov.io/gh/symbol/symbol/tree/dev/client/catapult

[client-rest-job]: https://jenkins.symbolsyndicate.us/blue/organizations/jenkins/Symbol%2Fgenerated%2Fsymbol%2Fclient-rest/activity?branch=dev
[client-rest-lint]: https://jenkins.symbolsyndicate.us/buildStatus/icon?job=Symbol%2Fgenerated%2Fsymbol%2Fclient-rest%2Fdev%2F&config=client-rest-lint
[client-rest-test]: https://jenkins.symbolsyndicate.us/buildStatus/icon?job=Symbol%2Fgenerated%2Fsymbol%2Fclient-rest%2Fdev%2F&config=client-rest-test
[client-rest-cov]: https://codecov.io/gh/symbol/symbol/branch/dev/graph/badge.svg?token=SSYYBMK0M7&flag=client-rest
[client-rest-cov-link]: https://codecov.io/gh/symbol/symbol/tree/dev/client/rest

[sdk-javascript-job]: https://jenkins.symbolsyndicate.us/blue/organizations/jenkins/Symbol%2Fgenerated%2Fsymbol%2Fsdk-javascript/activity?branch=dev
[sdk-javascript-lint]: https://jenkins.symbolsyndicate.us/buildStatus/icon?job=Symbol%2Fgenerated%2Fsymbol%2Fsdk-javascript%2Fdev%2F&config=sdk-javascript-lint
[sdk-javascript-build]: https://jenkins.symbolsyndicate.us/buildStatus/icon?job=Symbol%2Fgenerated%2Fsymbol%2Fsdk-javascript%2Fdev%2F&config=sdk-javascript-build
[sdk-javascript-test]: https://jenkins.symbolsyndicate.us/buildStatus/icon?job=Symbol%2Fgenerated%2Fsymbol%2Fsdk-javascript%2Fdev%2F&config=sdk-javascript-test
[sdk-javascript-examples]: https://jenkins.symbolsyndicate.us/buildStatus/icon?job=Symbol%2Fgenerated%2Fsymbol%2Fsdk-javascript%2Fdev%2F&config=sdk-javascript-examples
[sdk-javascript-vectors]: https://jenkins.symbolsyndicate.us/buildStatus/icon?job=Symbol%2Fgenerated%2Fsymbol%2Fsdk-javascript%2Fdev%2F&config=sdk-javascript-vectors
[sdk-javascript-cov]: https://codecov.io/gh/symbol/symbol/branch/dev/graph/badge.svg?token=SSYYBMK0M7&flag=sdk-javascript
[sdk-javascript-cov-link]: https://codecov.io/gh/symbol/symbol/tree/dev/sdk/javascript
[sdk-javascript-package]: https://img.shields.io/npm/v/symbol-sdk-javascript
[sdk-javascript-package-link]: https://www.npmjs.com/package/symbol-sdk-javascript

[sdk-python-job]: https://jenkins.symbolsyndicate.us/blue/organizations/jenkins/Symbol%2Fgenerated%2Fsymbol%2Fsdk-python/activity?branch=dev
[sdk-python-lint]: https://jenkins.symbolsyndicate.us/buildStatus/icon?job=Symbol%2Fgenerated%2Fsymbol%2Fsdk-python%2Fdev%2F&config=sdk-python-lint
[sdk-python-build]: https://jenkins.symbolsyndicate.us/buildStatus/icon?job=Symbol%2Fgenerated%2Fsymbol%2Fsdk-python%2Fdev%2F&config=sdk-python-build
[sdk-python-test]: https://jenkins.symbolsyndicate.us/buildStatus/icon?job=Symbol%2Fgenerated%2Fsymbol%2Fsdk-python%2Fdev%2F&config=sdk-python-test
[sdk-python-examples]: https://jenkins.symbolsyndicate.us/buildStatus/icon?job=Symbol%2Fgenerated%2Fsymbol%2Fsdk-python%2Fdev%2F&config=sdk-python-examples
[sdk-python-vectors]: https://jenkins.symbolsyndicate.us/buildStatus/icon?job=Symbol%2Fgenerated%2Fsymbol%2Fsdk-python%2Fdev%2F&config=sdk-python-vectors
[sdk-python-cov]: https://codecov.io/gh/symbol/symbol/branch/dev/graph/badge.svg?token=SSYYBMK0M7&flag=sdk-python
[sdk-python-cov-link]: https://codecov.io/gh/symbol/symbol/tree/dev/sdk/python
[sdk-python-package]: https://img.shields.io/pypi/v/symbol-sdk-python
[sdk-python-package-link]: https://pypi.org/project/symbol-sdk-python

[jenkins-job]: https://jenkins.symbolsyndicate.us/blue/organizations/jenkins/Symbol%2Fgenerated%2Fsymbol%2Fjenkins/activity?branch=dev
[jenkins-lint]: https://jenkins.symbolsyndicate.us/buildStatus/icon?job=Symbol%2Fgenerated%2Fsymbol%2Fjenkins%2Fdev%2F&config=jenkins-lint

[linters-job]: https://jenkins.symbolsyndicate.us/blue/organizations/jenkins/Symbol%2Fgenerated%2Fsymbol%2Flinters/activity?branch=dev
[linters-lint]: https://jenkins.symbolsyndicate.us/buildStatus/icon?job=Symbol%2Fgenerated%2Fsymbol%2Flinters%2Fdev%2F&config=linters-lint
