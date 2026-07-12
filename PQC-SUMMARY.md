# BNL Post-Quantum Catapult — PQC 作業総括資料

**対象**: BNL — Post-Quantum Catapult（Symbol/catapult の非公式・実験的フォーク。公式 Symbol/NEM とは無関係）
**期間**: 2026-07-05 〜 2026-07-10
**作成日**: 2026-07-10（最終更新: 2026-07-11）
**ステータス**: 全コンポーネント実装・実チェーン検証・公開まで完了（ランチャー・Explorer SMD 対応を含む）

English version: [`PQC-SUMMARY.en.md`](PQC-SUMMARY.en.md)

---

## 1. プロジェクト概要

Symbol/catapult ブロックチェーンの暗号基盤を、量子計算機に対して安全な NIST 標準
ポスト量子暗号（PQC）へ全面移行した。単なる署名関数の差し替えではなく、
**コンセンサス（ブロック抽選・ファイナリティ投票）・鍵交換・TLS・ワイヤフォーマット・
ステート・REST・SDK・運用ツールまで全レイヤー**を対象とし、新 nemesis のプライベート
ネットワークとしてフルスタックで動作することを実チェーンで検証した。

### 最終的な暗号構成

| 用途 | 旧（Symbol 公式） | 新（BNL PQC） | 標準 | 根拠・備考 |
|---|---|---|---|---|
| アカウント署名（Tx / ブロック） | ed25519 | **ML-DSA-44** | FIPS 204 | OpenSSL 3.5+ ネイティブ。秘密鍵は 32B seed 運用を維持 |
| ブロック抽選 VRF | ECVRF（edwards25519） | **iVRF**（ハッシュベース indexed VRF、SHA3-256 Merkle 木） | — （AsiaCCS 2023 / eprint 2022/993） | 標準 PQ-VRF が存在しないため。安全性はハッシュ仮定のみ |
| ファイナリティ投票 | ed25519（BM 2層ツリー内部） | **ML-DSA-44**（BM ツリー構造は温存 = 前方安全性維持） | FIPS 204 | |
| 鍵交換（委任ハーベスティング / 暗号化メッセージ） | X25519 ECDH | **ML-KEM-768** | FIPS 203 | KEM 化に伴いメッセージ形式に暗号文 1088B を同送 |
| ノード間 TLS | ed25519 証明書 | **ML-DSA-44 証明書** | FIPS 204 | TLS1.3 ハンドシェイクの ECDHE(X25519) は証明書署名と独立のため温存 |

### サイズへの影響（移行のすべての困難の源泉）

| 項目 | ed25519 | ML-DSA-44 / iVRF | 増分 |
|---|---|---|---|
| 秘密鍵 | 32 B | 32 B（seed 運用） | ±0 |
| 公開鍵 `Key` | 32 B | **1312 B** | +1280 B |
| 署名 `Signature` | 64 B | **2420 B** | +2356 B |
| Tx ヘッダ（署名者鍵+署名） | 96 B | 3732 B | +3636 B / tx |
| VRF proof（ブロックヘッダ） | 80 B（ECVRF） | **1056 B**（iVRF leaf 32B + path 32×32B 固定確保） | +976 B / block |
| VotingKey / VotingSignature | 32 B / 64 B | 1312 B / 2420 B | 投票 proof 約 38 倍 |

鍵・署名サイズが桁違いに変わるため既存チェーンとの互換維持は不可能であり、
**新規ネットワーク（新 nemesis）としての立ち上げ**を前提とした（既存 Symbol ネットワークとはプロトコル非互換）。

---

## 2. 作業タイムライン

| 日付 | フェーズ | 成果 |
|---|---|---|
| 07-05 | 環境構築 | フォーク取得、ビルドベースイメージ（gcc-15 / OpenSSL 3.6.2）、BNL イメージビルドスクリプト |
| 07-08 | 調査・設計 | 移行手順書（`ML-DSA-44-migration.md`）、FN-DSA 比較調査、VRF 暫定 PQ 化調査 |
| 07-08 | コア移行・起動 | C++ 暗号コア ML-DSA 化、新 nemesis 生成、**2 ノード実チェーンで block hash 一致同期を確認** |
| 07-08 | REST 投入経路 | REST `PUT /transactions` で ML-DSA Tx を投入 → **ブロック取込（confirmed）を実証** |
| 07-09 | SDK / bootstrap 調査 | JS SDK の ML-DSA 署名・アドレス導出・直列化を C++ とバイト一致で検証。bootstrap 対応方針確定 |
| 07-09〜10 | コンセンサス PQC 化 | 投票の ML-DSA 化、iVRF プリミティブ実装 → コンセンサス統合 → **2 ノード実チェーン検証** |
| 07-10 | フルスタック / 運用 | docker-compose 全経路検証、symbol-bootstrap 標準コマンド対応、SDK の ML-KEM メッセージ暗号化 |
| 07-10 | 公開 | Docker Hub 2 イメージ、GitHub 4 リポジトリ（catapult / SDK / explorer / bootstrap）公開 |
| 07-11 | ランチャー PQC 化 | BNL（blockchain-network-launcher）を **PQC 専用ランチャー**に改修、受け入れ検証 8 項目合格。**ML-DSA 投票鍵の実チェーン初検証** |
| 07-11 | Explorer SMD 統合 | explorer-smd `main` の SMD 機能を PQC explorer にマージ、公式ロゴ/ファビコン除去、SPA/REST ルーティング修正 |

---

## 3. コンポーネント別の変更内容

### 3-1. catapult-server（C++ / `client/catapult`）

**暗号コア（Phase 1）**
- `types.h`: `Key` 32→1312、`Signature` 64→2420、`VotingKey` 32→1312。VRF 鍵は `VrfPublicKey`（32B）として**型分離**
- `crypto/Signer.cpp`: donna ed25519 → OpenSSL EVP（`EVP_PKEY_ML_DSA_44`）。ed25519 固有の S 部正規化検証は削除
- `crypto/KeyPair.cpp`: 32B seed → `OSSL_PKEY_PARAM_ML_DSA_SEED` による決定的鍵展開（秘密鍵形式・バックアップ運用を温存）
- `crypto/OpensslKeyUtils.cpp`: `get_raw_*_key` が ML-DSA 非対応のため octet-string param / seed param 経由へ書換
- バッチ署名検証（`VerifyMulti`）: donna バッチ検証 → 単純ループへ（ML-DSA に バッチ検証は無い）
- `crypto/SharedKey.cpp`: X25519 ECDH → **ML-KEM-768** カプセル化（委任ハーベスティング復号経路を `MlKemKeyPair` 化）

**iVRF（ブロック抽選）**
- `crypto/iVrf.{h,cpp}` 新設: 深さ可変（既定 2^16）SHA3-256 Merkle 木。
  `leaf(i) = SHA3-256("catapult-ivrf-leaf" || seed || i_le64)`、proof = leaf + 認証パス、
  generation hash = `SHA3-256(leaf_i || 親GenerationHash)`。gtest（改竄/異 index/範囲外/異 seed 拒否、決定性）付き
- ブロックヘッダ: `GenerationHashProof`（ECVRF 80B）→ `iVrfProof`（固定 1056B。config depth を変えてもヘッダ形式不変）
- account state: VRF supplemental に **root + activation height** を格納。VrfKeyLink 確定時に observer が
  activation height（= link 高さ + `iVrfActivationDelay`）を記録（root 狙い撃ち登録の grinding 対策）
- 検証: `BlockchainProcessor` / `NemesisBlockLoader` が root・有効窓・`index = height − activationHeight` で検証
- harvester: アカウント毎に木をキャッシュ（構築 ~290ms/4MB）して `prove(index)`
- `KeyPair.cpp`: **VRF 公開鍵の導出 = seed から構築した iVRF 木の root**（`a5c1c36`。bootstrap 互換の要）
- config: `[chain] iVrfTreeDepth`（既定 16）/ `iVrfActivationDelay`（既定 0）を追加

**ファイナリティ投票**
- `crypto_voting`: `VotingSigner` / `VotingKeyPair` を ML-DSA へ委譲（`5717ca868`）。
  Bellare-Miner 2 層ツリー構造（エポック/ステップ前方安全性）は温存。
  `BmPrivateKeyTree` / `FinalizationMessage` は `::Size` 参照で自動追従

**TLS / ツール**
- `CertificateUtils.cpp`: `EVP_PKEY_ED25519` ハードコード → ML-DSA-44 判定。ノード identity = ML-DSA 公開鍵 1312B
- `tools/nemgen`: ML-DSA 署名 nemesis + iVRF proof(index 0) 生成、`nemesisSignerVrfPrivateKey` 追加、iVRF root をログ出力
- `tools/linker` / `AccountPrinter` / mongo `AccountStateMapper`（`Key`/`VrfPublicKey` 両対応テンプレート化）ほか追従

### 3-2. REST ゲートウェイ（`client/rest`）

- `src/index.js`: `sshpk` が ML-DSA 鍵（OID 2.16.840.1.101.3.4.3.17）を解釈できず起動不能
  → Node 標準 `crypto`（OpenSSL 3.5+）で SPKI DER から公開鍵抽出に置換
- block ルート/スキーマ: iVRF proof（`iVrfProofLeaf` + `iVrfProofPath`）を公開、旧 `proofGamma` 系を廃止
- `symbol-sdk` は PQC 版（ローカル→現在は GitHub の `pqc-catapult-sdk-v2#feat-pqc`）を使用
- 設計上の重要な確認: `PUT /transactions` は payload を**不透明転送**するため、announce 経路では
  catbuffer サイズ追従が不要（GET 系のデシリアライズでのみ必要）
- 運用: REST には API ノードと**独立した証明書 identity** が必須（流用すると identity 重複で拒否される）

### 3-3. JavaScript SDK v3（`sdk/javascript` → 公開: `pqc-catapult-sdk-v3`）

- `CryptoTypes.js` / `models.js`（catbuffer）: PublicKey 1312 / Signature 2420 / `VrfPublicKey`(32B) 型追加 /
  `VotingPublicKey` 1312。block モデルは iVRF proof（leaf + path、固定 1056B）
- `KeyPair.js`: `@noble/post-quantum` の `ml_dsa44`（**0.4.1 固定**。0.5.x は sign の引数順が逆）
- `SharedKey.js` / `MessageEncoder.js`: **ML-KEM-768** ベースへ全面書き換え。
  seed 導出 `SHA512("catapult-mlkem-seed" || privateKey)[:64]`、HKDF-SHA256、
  メッセージ形式 `[marker][cipherText 1088][tag][iv][AES-GCM 暗号文]`（C++ ノードと相互運用）
- `symbol/iVrf.js`: C++ と同一の iVRF 実装（root/leaf/path/generationHash が **byte 単位一致**）
- `VotingKeysGenerator`: 鍵/署名サイズから動的算出（C++ `BmPrivateKeyTree` と byte 一致）
- `utils/converter.js`: 2420B 署名が 8B 整列を崩すため非整列読み出しフォールバック追加
- 単独リポジトリ公開: `git subtree split` で履歴を保持したまま `pqc-catapult-sdk-v3` に切り出し、
  `iVrf` を `symbol-sdk/symbol` エントリポイントから export。純 ESM・ビルド不要のため
  `npm i github:bootarou/pqc-catapult-sdk-v3#feat-pqc` がそのまま動く

**旧 SDK v2**（`pqc-catapult-sdk-v2`、explorer 用）: 別系統の TypeScript SDK 2.0.6 も PQC 化。
npm git インストール対応として、壊れたサブモジュール（`catbuffer-generators` / `travis`、参照先消滅）を削除し
`prepare: npm run build` を追加 → `npm i github:bootarou/pqc-catapult-sdk-v2#feat-pqc` で利用可能

### 3-4. symbol-bootstrap（公開: `bootarou/symbol-bootstrap` ブランチ `pqc-bootstrap`）

**操作方法を変えずに**（`config` → `compose` → `up` の標準 CLI のまま）PQC チェーンを生成・運用可能にした:
- `CertificateService`: `openssl genpkey -algorithm ML-DSA-44 -pkeyopt hexseed:<seed>` による決定的鍵・証明書生成
- 鍵生成: ML-DSA アカウント導出（公開鍵・アドレスが catapult と一致）、VRF は iVRF root、voting は ML-DSA
- `ComposeService`: mongo データディレクトリを `/data/db` に修正（公式 entrypoint の chown 経路に乗せ権限問題を解消）
- `rest.json.mustache`: `routeExtensions: []` 追加（PQC REST 2.5.1 系の要求フィールド）
- `presets/shared.yml`: PQC イメージ（`nftdrive/bnl-catapult-*-pqc`）参照

**ランチャー対応で追加した修正（2026-07-11）**: `VotingUtils` を **ML-DSA-44 投票鍵ツリー対応**に書き換え
（root 公開鍵 1312B / 署名 2420B、ヘッダ 48+1312。旧 ed25519 サイズ検証が PQC の
`catapult.tools.votingkey` 出力 1,766,800B を拒否していた — voting 有効ノードは従来未踏の経路）。
あわせて幽霊依存（`tweetnacl` / `symbol-openapi-typescript-fetch-client`）の明示宣言と
`bin/run` 実行ビットの復元。

### 3-5. Explorer（公開: `pqc-catapult-explorer` ブランチ `feat-pqc`）

- iVRF ブロック proof の表示対応（`proofGamma` 系を廃止し `iVrfProofLeaf`/`iVrfProofPath`）、
  PQC symbol-sdk v2 への切替（`github:bootarou/pqc-catapult-sdk-v2#feat-pqc`）
- **SMD 統合（2026-07-11）**: explorer-smd `main` の SMD 機能（ソーシャルメタデータ、SMD 一覧、
  モザイク詳細のホルダー一覧/Tx 履歴）をマージ。現在の feat-pqc は **NFTDrive SMD 版 + PQC** の統合版
  （計画・検証記録: リポジトリ内 `PQC-SMD-PLAN.md`）
- **商標対応**: Symbol 公式ロゴ（ヘッダーワードマーク・メニュー×3）を「BNL PQC」テキストに置換、
  公式ファビコン一式を削除。フッターは BNL リポジトリへの GitHub リンクのみ
- SMD 側の一覧表示が nemesis の鍵リンク Tx を初めて踏んだことで、SDK v2 の
  `linkedPublicKey` 64 hex 固定検証を発見 → **2624 hex（ML-DSA）対応を SDK 側に追加**
  （Account/Node/Voting。VRF は 32B root のため 64 のまま）

### 3-6. 支援ツール（リポジトリ外 `/home/user/catapult/`）

| ツール | 内容 |
|---|---|
| `poc/` | C++ 単体 PoC（鍵生成 `netgen_keys`、Tx 署名 `txsign`、p2p 投入 `txpush`、ML-DSA 検証等） |
| `jssign/` | 純 JS の Tx ビルダー（`txbuild` / `vrflink` / `votinglink` / `addr`、ML-DSA 署名） |
| `net/` | 実験ネットワーク一式（鍵・ML-DSA 証明書・config・nemesis seed・ノードデータ） |
| `build-target.sh` / `build-bnl-image.sh` | 個別ターゲット / リリースイメージのビルド |
| `docker-compose.pqc.yml` | mongo + node-api + broker + REST のフルスタック起動（リポジトリにも同梱） |

### 3-7. BNL ランチャー（blockchain-network-launcher ブランチ `feat-PQC-custom-catapult`）

Web UI からの PQC ネットワーク構築・運用を可能にする **PQC 専用ランチャー**
（切替式ではなく専用化。判断根拠と全計画・検証記録はランチャーリポジトリの
`docs/PQC-LAUNCHER-PLAN.md`）。**このブランチは main/dev 等へマージしない運用**。

- **pqc-bootstrap をイメージに同梱**: `npm install -g <git-url>` の不安定さ（npm 10.8 の
  symlink バグ・`files` ホワイトリスト・prepack）を避け、git clone + `npm install --omit=dev` +
  bin symlink 方式。ビルド時に「ML-DSA 版であること」をサニティチェックし、実行時も
  npx キャッシュ/レジストリへのフォールバックを禁止（旧 ed25519 版が黙って動く事故を根絶）
- **Catapult バージョンを `pqc` 単一に**（`nftdrive/bnl-catapult-*-pqc` イメージ）。
  `chainFinalizationHeight` は Configuration UI から設定可能な組み込み項目に昇格
- **UI の PQC 専用化**: 公式 mainnet/testnet 参加は取り込み時に明示拒否、
  2624 hex 公開鍵の省略表示、「BNL Post-Quantum Network Manager」へブランド変更
- **Explorer 統合**: `pqc-catapult-explorer#feat-pqc` をビルド・起動。生成プロキシに
  ①ブランチ tip でのキャッシュバスト、②多段ビルド化（依存不変なら `npm install` を
  キャッシュ再利用 — 無変更再ビルド約 5 秒、ソースのみ変更 2〜3 分）、
  ③**Accept ヘッダによる SPA/REST ルーティング**（history モードの SPA と REST のパス衝突を解消。
  旧系統 `main`/`feat-custom-catapult` にも独立コミットとして適用済み）、
  ④HTML/`/config` の no-cache 化
- **受け入れ検証（8 項目合格）**: UI 相当 API からのネットワーク作成 → iVRF/ML-DSA 採掘 →
  `chainFinalizationHeight=15` で正確に凍結 → 停止/再開 → バックアップ → Explorer 閲覧。
  **voting 有効ノード（ML-DSA 投票鍵の生成・nemesis リンク）を実チェーンで初検証**

---

## 4. 検証結果サマリ

| # | 検証 | 結果 | 決定的証跡 |
|---|---|---|---|
| 1 | ML-DSA 新 nemesis からのノード起動・連続ブロック生成 | ✅ | `loaded blockchain (height=1)` → 連続 harvest |
| 2 | 2 ノード同期（ML-DSA） | ✅ | block hash 完全一致（h5:`927B4078` 等）、双方向 Push_Block |
| 3 | ML-DSA Tx の REST 投入 → ブロック取込 | ✅ | `PUT /transactions` 202 → height 367 で confirmed（非空ブロック 7KB） |
| 4 | ML-DSA 証明書での相互 TLS | ✅ | ピア間 / REST（Node 22）⇄ ノード間で成立 |
| 5 | 投票（ML-DSA BM ツリー）ライブラリ検証 | ✅ | 署名/検証・改竄拒否・直列化往復 PASS、SDK 生成鍵ツリーと C++ が byte 一致 |
| 6 | iVRF 2 ノード実チェーン | ✅ | nemesis iVRF root 検証ロード → 連続 harvest → node-b が proof 検証・chain score 一致、エラー 0 |
| 7 | C++ ⇄ JS SDK の iVRF 相互検証 | ✅ | 実オンチェーンブロックの deserialize→re-serialize バイト完全一致、leaf が SDK `computeLeaf` と一致 |
| 8 | ML-KEM メッセージ暗号化の相互運用 | ✅ | noble ⇄ OpenSSL で shared secret 一致、AES-GCM 改竄検出、委任往復一致 |
| 9 | フルスタック（node+broker+mongo+REST） | ✅ | `GET /blocks/2` が iVRF proof を返却、`/chain/info` 高さ増加 |
| 10 | symbol-bootstrap 標準コマンドでの PQC ネット | ✅ | `config -a dual` → `compose` → `up` で高さ 155+ 連続採掘、エラー 0 |
| 11 | BNL ランチャー受け入れ（8 項目） | ✅ | UI 経由ネットワーク作成 / iVRF 採掘 / 高さ 15 凍結（30 秒×4 回）/ 再開 / バックアップ（ML-DSA 鍵入り）|
| 12 | SMD Explorer 実チェーン | ✅ | ホルダー一覧が ML-DSA 公開鍵 2624 hex を返却、iVRF proof 表示、鍵リンク Tx 一覧表示（SDK 修正後） |

---

## 5. 公開物一覧（すべて公開済み・2026-07-10 検証）

### GitHub（アカウント: `bootarou`）

| リポジトリ | ブランチ | 内容 |
|---|---|---|
| [bnl-catapult-pqc](https://github.com/bootarou/bnl-catapult-pqc) | `feat-VRF/votiong` | 本体モノレポ（catapult-server / REST / SDK / ドキュメント） |
| [pqc-catapult-sdk-v3](https://github.com/bootarou/pqc-catapult-sdk-v3) | `feat-pqc` | **JavaScript SDK v3**（モノレポ `sdk/javascript` を履歴ごと切り出し。純 ESM・ビルド不要、`npm i github:bootarou/pqc-catapult-sdk-v3#feat-pqc` で利用可） |
| [pqc-catapult-sdk-v2](https://github.com/bootarou/pqc-catapult-sdk-v2) | `feat-pqc` | TypeScript SDK v2（旧 symbol-sdk 2.x 系 PQC 版。explorer が使用。npm git 依存として利用可） |
| [pqc-catapult-explorer](https://github.com/bootarou/pqc-catapult-explorer) | `feat-pqc` | ブロックエクスプローラ（**NFTDrive SMD 版 + PQC 統合**。iVRF proof 表示、公式商標物除去済み） |
| [symbol-bootstrap](https://github.com/bootarou/symbol-bootstrap) | `pqc-bootstrap` | ネットワーク生成・運用 CLI の PQC 対応 |
| [blockchain-network-launcher](https://github.com/bootarou/blockchain-network-launcher) | `feat-PQC-custom-catapult` | **BNL 本体の PQC 専用ランチャー**（Web UI。main 等へマージしない独立系統） |

### Docker Hub（ネームスペース: `nftdrive`、linux/amd64）

| イメージ | ダイジェスト（Hub = ローカル検証済） | 役割 |
|---|---|---|
| `nftdrive/bnl-catapult-server-pqc:1.0.3.9-bnl` | `sha256:620b960636ed…` | catapult server + broker（3.95 GB） |
| `nftdrive/bnl-catapult-rest-pqc:2.4.3-bnl` | `sha256:570ee2e29bc3…` | REST ゲートウェイ（726 MB） |

mongo は公式 `mongo:5.0.15` を無改変で使用（プッシュ対象外）。詳細は [`PQC-images.md`](PQC-images.md)。

---

## 6. 主要な設計判断と根拠

1. **ML-DSA-44 の選定**（vs FN-DSA-512）: FIPS 204 最終化済み・OpenSSL ネイティブ・実装リスク小を優先。
   FN-DSA はサイズで約 2.5 倍優れるが FIPS 206 未確定・OpenSSL 非対応・浮動小数点実装リスクのため
   第 2 世代ネットワークの選択肢として保留（詳細: `FN-DSA-migration-diff.md`）
2. **iVRF の採用**（vs ECVRF 温存 / NIS1 方式 / LaV）: 標準 PQ-VRF が存在しない中、ハッシュ仮定のみで
   一意性・秘匿性・検証可能性を満たす唯一の実用解。proof ~1KB は LaV(12KB) 等より遥かに小さい。
   高さ窓 + 再登録 + activation delay で grinding を防止（詳細: `VRF-PQ-interim-options.md`）。
   なお X-VRF(XMSS 系) は uniqueness 攻撃（FC'24）で既に破られており除外
3. **秘密鍵 32B seed 運用の維持**: 鍵バックアップ・BIP32 派生・設定ファイル形式・bootstrap の
   鍵管理をすべて温存できる（ML-DSA / ML-KEM / iVRF すべて seed から決定的導出）
4. **VRF 公開鍵 = iVRF 木の root**: 既存の VrfKeyLink Tx・AccountPublicKeys スキーマ（32B）を
   無改造で流用でき、bootstrap の鍵リンク生成もそのまま動く
5. **proof フィールドの固定長化**（depth 32 分 = 1056B 確保）: config で木の深さを変えても
   ブロックヘッダ形式が不変。将来 LaV 等への差し替え余地も確保
6. **新 nemesis 前提の割り切り**: 全ハッシュ・全アドレスが変わるため既存チェーン互換は原理的に不可能。
   BNL のようなプライベート/実験ネットワーク専用と明示（README に非公式フォーク免責を掲示）

---

## 7. 既知の制約・残課題

| # | 項目 | 状態 |
|---|---|---|
| 1 | crypto 系 gtest / SDK テストベクタが ed25519 前提のまま | 未更新（ランタイムは e2e で検証済み）。NIST ACVP ベクタへの置換が正攻法 |
| 2 | ~~BFT ファイナリティのエポック進行の実チェーン観測~~ | **解消（2026-07-12）**: エポック 1→2 進行と ML-DSA 証明（15,320 B）を実観測（`PQC-size-report.md` §6） |
| 3 | `enableVerifiableState=true` での state hash 一致検証 | bootstrap 生成ネットワークでは有効化済みで安定稼働（複数ノード間の明示的一致検証は未実施） |
| 4 | iVRF 窓満了（2^16 ブロック）時の再登録の運用検証 | 未実施（機構は実装済み） |
| 5 | ML-KEM 公開鍵（1184B）の配布経路 | アカウント公開鍵から導出不可のため別途入手が必要。オンチェーン公開の仕組みは未設計 |
| 6 | イメージサイズ削減（server 3.95GB）・arm64 マルチアーチ | 未着手（`PQC-images.md` §5） |
| 7 | VRF grinding 対策の `iVrfActivationDelay` 既定値 | 0（無効）。本番運用時は importance grouping 相当の遅延を設定すべき |
| 8 | 容量関連の preset 調整（`maxTransactionsPerBlock` 6,000 は `maxBlockCacheSize` 10MB と不整合） | 未対応。推奨値は `PQC-size-report.md` §5 |

---

## 8. ドキュメント索引

### リポジトリ内（`symbol/`）
| 文書 | 内容 |
|---|---|
| [`README.md`](README.md) | プロジェクト概要・免責 |
| [`BNL-STARTUP.md`](BNL-STARTUP.md) | ビルド〜フルスタック起動・検証の実務マニュアル・トラブルシューティング |
| [`PQC-VRF-voting-report.md`](PQC-VRF-voting-report.md) | 投票 ML-DSA 化・iVRF 実装/統合/実チェーン検証の詳細 |
| [`ML-DSA-44-sdk-report.md`](ML-DSA-44-sdk-report.md) | JS SDK（署名・catbuffer・ML-KEM メッセージ暗号化） |
| [`PQC-bootstrap-report.md`](PQC-bootstrap-report.md) | symbol-bootstrap 対応と検証 |
| [`PQC-images.md`](PQC-images.md) | Docker イメージ一覧・公開状況 |
| [`PQC-size-report.md`](PQC-size-report.md) | ブロック・Tx・ストレージの容量実測と成長予測、設定上限との整合性 |

### 作業ディレクトリ（`/home/user/catapult/`、リポジトリ外の調査・作業記録）
| 文書 | 内容 |
|---|---|
| `ML-DSA-44-migration.md` | 全レイヤー移行手順書（Phase 0〜8。本作業の設計図） |
| `ML-DSA-44-bringup-report.md` | コア移行後の初回実チェーン起動・2 ノード同期検証 |
| `ML-DSA-44-rest-report.md` | REST 経由 Tx 投入 → ブロック取込の実証 |
| `ML-DSA-44-bootstrap-report.md` | bootstrap ML-DSA 化の事前調査・実証コード |
| `VRF-PQ-interim-options.md` | VRF の PQ 化選択肢の調査（iVRF 採用の根拠） |
| `FN-DSA-migration-diff.md` | ML-DSA-44 vs FN-DSA-512 の差分調査（アルゴリズム選定の根拠） |

### 他リポジトリの計画・検証記録
| 文書 | 内容 |
|---|---|
| [launcher `docs/PQC-LAUNCHER-PLAN.md`](https://github.com/bootarou/blockchain-network-launcher/blob/feat-PQC-custom-catapult/docs/PQC-LAUNCHER-PLAN.md) | ランチャー PQC 専用化の計画書 + 受け入れ検証結果 |
| [explorer `PQC-SMD-PLAN.md`](https://github.com/bootarou/pqc-catapult-explorer/blob/feat-pqc/PQC-SMD-PLAN.md) | SMD 統合の改修計画書 + 検証結果 |
