# BNL Catapult 成果物マップ — 4 世代の系譜と配布物

BNL(Post-Quantum Catapult プロジェクト)がこれまでに作った catapult 派生 4 世代と、
それぞれのリポジトリ・ブランチ・Docker イメージ・周辺ツールの対応表。
(2026-07-14 時点。各系譜の git 祖先関係は `git merge-base --is-ancestor` で検証済み)

> ⚠️ これらは Symbol 公式とは無関係の**非公式フォーク**です。①以外は公開 Symbol ネットワークに参加できません。

---

## 1. 系譜(catapult-server 本体)

```
上流 Symbol (symbolplatform/symbol, catapult v1.0.3.9)
  │
  ├─① 通常 Symbol 版(公式そのまま)
  │
  └─② + chainFinalizationHeight(非PQC)                 7734b8da6
        │   repo: bootarou/custom-catapult-chainFinalizationHeight
        │        (main = 上流 + 安定化修正 + chainFinalizationHeight)
        │
        ├─②' + emptyBlockPolicy(非PQC)                  313145613
        │     branch: feat-empty-block-policy(同リポジトリ)
        │     ④ の実装から iVRF 固有部分を除いた移植
        │
        └─③ + PQC 化(ML-DSA-44 / iVRF / ML-KEM-768 / ML-DSA voting)
              │   repo: bootarou/bnl-catapult-pqc
              │   作業ブランチ: feat-VRF/votiong
              │   ※ ② の chainFinalizationHeight を含んだまま PQC 化
              │
              └─④ + emptyBlockPolicy                     31a1b5a58
                    branch: feat-empty-block-policy
                    (+ アイドルチェーンの Tx 受付デッドロック修正)
```

機能は**累積**する: ④ = 上流 + 安定化修正 + chainFinalizationHeight + PQC 一式 + emptyBlockPolicy。

## 2. 成果物マップ(世代 × 配布物)

| | ① 通常 Symbol | ② +chainFinalization | ②' 非PQC + emptyBlockPolicy | ③ PQC 版 | ④ PQC + emptyBlockPolicy |
|---|---|---|---|---|---|
| **catapult リポジトリ** | [symbolplatform/symbol](https://github.com/symbol/symbol) | [custom-catapult-chainFinalizationHeight](https://github.com/bootarou/custom-catapult-chainFinalizationHeight)(main) | 同 `feat-empty-block-policy`(313145613) | [bnl-catapult-pqc](https://github.com/bootarou/bnl-catapult-pqc) `feat-VRF/votiong` | 同 `feat-empty-block-policy`(31a1b5a58) |
| **server イメージ** | `symbolplatform/symbol-server:gcc-1.0.3.9` | `nftdrive/bnl-catapult-server:1.0.3.9-cf1` | `nftdrive/bnl-catapult-server:1.0.3.9-cf1-ebp` | `nftdrive/bnl-catapult-server-pqc:1.0.3.9-bnl` | `nftdrive/bnl-catapult-server-pqc:1.0.3.9-bnl-ebp` |
| **REST イメージ** | `symbolplatform/symbol-rest:2.4.3` | (公式流用) | (公式流用) | `nftdrive/bnl-catapult-rest-pqc:2.4.3-bnl` | ③と同じ(変更不要) |
| **SDK (JavaScript)** | 公式 symbol-sdk | (公式流用) | (公式流用) | [pqc-catapult-sdk-v2](https://github.com/bootarou/pqc-catapult-sdk-v2) / [pqc-catapult-sdk-v3](https://github.com/bootarou/pqc-catapult-sdk-v3)(各 `feat-pqc`) | ③と同じ(変更不要) |
| **symbol-bootstrap** | 公式 | — | — | [bootarou/symbol-bootstrap](https://github.com/bootarou/symbol-bootstrap) `pqc-bootstrap` | 同 `feat-empty-block-policy`(f071503) |
| **BNL launcher** | [blockchain-network-launcher](https://github.com/bootarou/blockchain-network-launcher) main / dev | `feat-custom-catapult` | `feat-empty-block-policy-cf`(75a30bf) | `feat-PQC-custom-catapult` | 同 `feat-empty-block-policy`(a6f01bc) |
| **explorer** | 公式 symbol-explorer | — | — | [pqc-catapult-explorer](https://github.com/bootarou/pqc-catapult-explorer) `feat-pqc`(SMD 統合済み) | ③と同じ |

④ で REST / SDK / explorer に変更が不要なのは、emptyBlockPolicy が**ハーベスタのローカル動作**であり、
ワイヤフォーマット・ブロック検証ルール・API に影響しないため(実チェーンで検証済み)。

②' は ④ の実装から iVRF 固有部分(leaf 消費最適化のコメント等)を除いた移植で、config・判定ロジック・
Tx 受付デッドロック修正・テストは同一。非PQC はテストツリーが健全なため、PQC 側では実行保留になっている
ServiceStateTests(predicate テスト)も **11/11 全パス**で検証済み(model 23/23 / harvesting 24/24)。

②'系の bootstrap は**公式 symbol-bootstrap のまま**で、launcher が config 生成後に
プロパティを注入する(postGenPatches)。launcher `feat-empty-block-policy-cf` では
`CUSTOM_SERVER_IMAGE` に既知の BNL イメージを指定するだけで対応プロパティが**自動注入**される
(`*-cf<N>` → chainFinalizationHeight=0、`*-ebp` → +emptyBlockPolicy=heartbeat / 86400s。
UI「カスタム設定」で編集可、`CUSTOM_CONFIG_PATCHES` でキー単位の上書きも可)。

## 3. ④ emptyBlockPolicy 版の構成要素

- **catapult** `feat-empty-block-policy`:
  - `[chain]` に `emptyBlockPolicy = normal | suppress | heartbeat` と
    `emptyBlockHeartbeatInterval`(いずれも省略可、既定 normal = 従来互換)。
  - skip 判定は iVRF prove **前**に実行され、skip 時は iVRF leaf を消費しない
    (leaf index はブロック高さ紐付き)。
  - `CreateShouldProcessTransactionsPredicate` をポリシー対応化
    (抑制チェーンでは「最終ブロックが古い」ことが定常状態のため、
    未同期シグナルとして使うと Tx 受付デッドロックになる)。
- **bootstrap** `feat-empty-block-policy`: config-network テンプレートに 2 プロパティを追加、
  `presets/shared.yml` 既定 normal / 86400s、server イメージを `1.0.3.9-bnl-ebp` に。
- **launcher** `feat-empty-block-policy`: private/data chain preset の既定を **heartbeat / 86400s(推奨)**に。
  UI フィールド・chain key whitelist 追加。Dockerfile の同梱 bootstrap 既定ブランチを
  `feat-empty-block-policy` に変更 → **このブランチを clone して `docker compose build` するだけで完結**。

## 4. 運用ルール(既存)

- `bnl-catapult-pqc` の `main`(28da985)は第 1 世代 PQC の保存地点 — **変更禁止**。
  作業は `feat-VRF/votiong` 系列へ。
- launcher の PQC 対応は `feat-PQC-custom-catapult` 系列限定 —
  **main / dev / feat-custom-catapult へのマージ禁止**。
- `custom-catapult-chainFinalizationHeight` は非 PQC の独立リポジトリとして維持
  (PQC を混ぜない)。

## 5. 関連ドキュメント

- PQC 全体サマリ: [PQC-SUMMARY.md](PQC-SUMMARY.md) / [PQC-SUMMARY.en.md](PQC-SUMMARY.en.md)
- 公開イメージの digest: [PQC-images.md](PQC-images.md)
- 起動手順: [BNL-STARTUP.md](BNL-STARTUP.md)
- emptyBlockPolicy の実装・検証レポート: リポジトリ外 `catapult/empty-block-policy-report.md`
  (config・判定ロジック・ユニットテスト・実チェーン全 7 条件の検証結果・iVRF への効果)
