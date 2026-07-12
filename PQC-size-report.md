# PQC チェーン容量調査レポート — ブロック・トランザクション・ストレージ実測

**対象**: BNL — Post-Quantum Catapult（ML-DSA-44 / ML-KEM-768 / iVRF / ML-DSA 投票）
**調査日**: 2026-07-12
**測定環境**: BNL ランチャーで稼働中の実チェーン（voting 有効 dual ノード、測定時高さ 63〜84、
`blockGenerationTargetTime = 30s`、`fileDatabaseBatchSize = 1`）。
理論値は PQC SDK v3 の catbuffer モデルで算出し、実測値と突き合わせて検証した。

---

## 1. 結論サマリ

- **トランザクションは約 22 倍**（transfer: 176 B → 3,812 B）。増分のほぼ全てが署名（+2,356 B）と公開鍵（+1,280 B）
- **ブロックヘッダは約 13 倍**（372 B → 4,988 B）。iVRF proof（+976 B）も寄与
- **空ブロックのディスク保存は約 7 倍**（実測 5,348 B/block ≒ 15.4 MB/日、5.6 GB/年）
- **手数料はサイズ課金のため同倍率で増加**（transfer 最小手数料 約 21.7 倍）
- 既定設定のままでは **`maxTransactionsPerBlock = 6,000` は実効不能**（理論最大ブロック 22.9 MB が
  `maxBlockCacheSize = 10MB` を超過）。実効上限は **約 2,600 tx/block（≒ 87 TPS）**


---

## 2. ワイヤフォーマット（理論値 = SDK v3 算出、実測で検証済み）

### トランザクション

| 項目 | 旧 Symbol | PQC | 倍率 | 備考 |
|---|---:|---:|---:|---|
| 共通 Tx ヘッダ（size〜deadline） | 128 B | **3,764 B** | 29.4× | 署名 2,420 + 公開鍵 1,312 |
| Transfer（モザイク1・メッセージ無） | 176 B | **3,812 B** | 21.7× | p2p packet 実測 3,820 B（+8 B packet ヘッダ）と一致 |
| Aggregate 外殻（内包0・連署0） | 168 B | **3,804 B** | 22.6× | |
| Cosignature（連署 1 件） | 104 B | **3,740 B** | 36.0× | 集約 Tx の連署人数に比例して加算 |
| Embedded Tx ヘッダ | 48 B | **1,328 B** | 27.7× | 集約内包 Tx（署名なし・公開鍵のみ） |
| AccountKeyLink / NodeKeyLink / VotingKeyLink | 161 B | **6,420〜6,421 B** | ≈40× | linkedPublicKey も 1,312 B のため二重に増加 |
| VrfKeyLink | 161 B | **5,141 B** | 31.9× | linked key は iVRF root（32 B）のまま |

### ブロック

| 項目 | 旧 Symbol | PQC | 倍率 | 備考 |
|---|---:|---:|---:|---|
| ブロックヘッダ（Normal） | 372 B | **4,988 B** | 13.4× | 署名 +2,356 / 署名者鍵 +1,280 / iVRF proof 80→1,056（+976） |
| ブロックヘッダ（Nemesis/Importance） | 424 B | **5,036 B** | 11.9× | |
| Transfer N 件のブロック（ワイヤ） | 372+176N | **4,988+3,812N** | — | 例: 100 tx = 386 KB（旧 18 KB） |

---

## 3. ストレージ実測（稼働チェーン）

### ブロックファイル DB（`data/00000/*.dat` ほか）

| ファイル | 実測 | 内訳・備考 |
|---|---:|---|
| 空ブロック `.dat` | **5,348 B** | ヘッダ 4,988 + 要素メタ 360（entity/generation hash、サブキャッシュ merkle root 群） |
| nemesis `.dat`（内包 Tx 20 件） | **85,281 B** | transfer 8・key link 6・mosaic/namespace 系 6 |
| ブロック statement `.stmt` | 128 B/block | |
| **ファイナリティ証明 `.proof`** | **15,320 B/エポック** | ML-DSA BM ツリー署名。エポック 2 の実物（エポック進行を初観測） |
| データディレクトリ全体（高さ 63 時点） | 10.5 MB | |

### MongoDB（REST 提供用の投影）

| コレクション | 平均ドキュメント | 備考 |
|---|---:|---|
| blocks | **6,036 B** | iVRF proof（leaf+path 2,048 hex）を含む |
| transactions | **4,307 B** | 測定対象は nemesis の 20 件（transfer〜voting link 混在） |
| accounts | 2,104 B | ML-DSA 公開鍵 2,624 hex を含む |
| REST JSON 応答（参考） | block 11,121 B / voting link Tx 10,603 B | hex 表現のため格納サイズの約 2 倍 |

### 鍵・証明書などの運用ファイル

| ファイル | 旧 Symbol | PQC 実測 | 倍率 |
|---|---:|---:|---:|
| 投票鍵ファイル（720 エポック） | 69,200 B | **1,766,800 B** | 25.5×（≒2,454 B/エポック） |
| ノード証明書 `node.crt.pem` | 〜0.8 KB | **5,437 B** | 〜7× |
| 証明書チェーン `node.full.crt.pem` | 〜1.5 KB | **10,886 B** | 〜7×（TLS ハンドシェイク毎に送信される点に注意） |
| addresses.yml（dual・暗号化） | 〜5 KB | **38,918 B** | 〜8× |

---

## 4. 成長予測（30 秒ブロック = 2,880 block/日）

| シナリオ | ブロックファイル | MongoDB | 合計目安 |
|---|---:|---:|---:|
| 空チェーン（アイドル） | 15.8 MB/日（5.8 GB/年） | 17.4 MB/日（6.3 GB/年） | **約 12 GB/年** |
| 平均 10 tx/block | +112 MB/日 | +130 MB/日 | 約 100 GB/年 |
| 実効満杯（2,600 tx/block, §5） | 29 GB/日 | — | ローカル実験の範囲外 |

- 旧 Symbol の空チェーンは約 2.1 MB/日 → **アイドル時で約 7.5 倍**
- ファイナリティ証明は 4 エポック/日（votingSetGrouping=720）× 15,320 B ≒ 60 KB/日で無視できる規模
- 同期帯域: 空ブロック同期は 360 block/回 ≒ 1.9 MB。満杯ブロックでは
  `maxChainBytesPerSyncAttempt = 100MB` が効き 1 回あたり約 10 block に自動制限される（設定は機能する）

## 5. プロトコル上限と設定の整合性【要注意】

現行 preset の組み合わせに **PQC 化で顕在化する不整合**がある:

| 設定 | 値 | PQC での意味 |
|---|---|---|
| `maxTransactionsPerBlock` | 6,000 | 理論最大ブロック = 4,988 + 6,000×3,812 ≒ **22.9 MB** |
| `maxBlockCacheSize` | **10 MB** | 上記が収まらない → **実効上限 ≒ 2,600 transfer/block（87 TPS）**。6,000 tx を詰めると同期・キャッシュで問題になり得る |
| `maxPacketDataSize` | 150 MB | 単発パケットとしては余裕 |
| `defaultDynamicFeeMultiplier` / `minFeeMultiplier` | 100 | transfer 最小手数料 = 3,812×100 = 0.3812 通貨単位（旧 0.0176、**21.7 倍**）。divisibility 6 のまま |

**推奨**（ネットワーク再生成時に preset へ反映するのが安全）:
1. `maxTransactionsPerBlock` を **2,000〜2,500** に引き下げる（または `maxBlockCacheSize` を 32MB 程度へ拡大）
2. 手数料の体感を旧チェーンに近づけたい場合は fee multiplier を 1/20 程度に調整
   （サイズ課金の仕組み自体は健全に機能しており、スパム耐性の観点では現状維持も合理的）
3. 集約 Tx を多用する設計では **連署 3,740 B/件** が支配的コストになる点を考慮
   （`maxCosignaturesPerAggregate = 25` 満載で連署だけで 93.5 KB）


## 6. 測定方法（再現手順）

- ワイヤ理論値: PQC SDK v3（`pqc-catapult-sdk-v3#feat-pqc`）の catbuffer モデルで `tx.size` を算出
- ブロックファイル: `fileDatabaseBatchSize = 1` のため `data/00000/NNNNN.dat` = 1 ブロック。`stat -c%s` で実測
- MongoDB: `db.<collection>.stats()` の `avgObjSize`
- クロスチェック: 実測 5,348 B（空 .dat）− SDK 算出ヘッダ 4,988 B = 要素メタ 360 B、
  過去の実測（ML-DSA transfer の p2p packet 3,820 B、bringup 時の非空ブロック 7,824 B）とも整合
