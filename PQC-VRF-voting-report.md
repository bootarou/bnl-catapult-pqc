# PQC 化: Finalization Voting と VRF（iVRF）

対象: post-quantum catapult フォーク
ブランチ: `feat-VRF/votiong`
更新: 2026-07-09

ハイブリッド設計の残件だったコンセンサス層（voting / VRF）を耐量子化する作業。
アカウント署名・TLS・委譲収穫鍵交換は既に ML-DSA-44 / ML-KEM-768 化済み。

---

## 1. Finalization Voting → ML-DSA-44 ✅ 完了・検証済み

前方安全な Bellare-Miner（BM）2層ツリー署名の内部 ed25519 を **ML-DSA-44** に差し替え、
BM ツリー構造（＝エポック/ステップの前方安全性）は温存。

**変更**
- `types.h`: `VotingKey` 32 → **1312**
- `crypto_voting`: `VotingSignature` 64 → **2420**（`VotingPrivateKey` は 32B ML-DSA seed のまま）。
  `VotingSigner` / `VotingKeyPair` を `MlDsaSign` / `MlDsaVerify` / `ExtractMlDsaPublicKey` に委譲。
  `BmPrivateKeyTree` / `PinnedVotingKey` / `FinalizationMessage` は `::Size` 経由で自動追従。
- SDK: `VotingKeysGenerator` を鍵/署名サイズから動的算出（ヘッダ1360B, エントリ2452B）。
  catbuffer `VotingPublicKey` 32 → 1312。

**検証**
- サーバ全体（server/broker/recovery/finalization/mongo/coresystem/nemgen）ビルド成功。
- 実 `crypto_voting` ライブラリでのスタンドアロン検証: BM ツリー署名/検証、改竄拒否、
  鍵ID束縛、直列化→再読込→署名/検証すべて PASS。
- SDK 生成の voting 鍵ツリーが C++ の `BmPrivateKeyTree` 配置と byte 単位一致、
  全エポックの子鍵署名が ML-DSA root 鍵で検証成功。

コミット: `5717ca868`（voting）, `23eef52b3`（test util 修正）

---

## 2. VRF（ブロック抽選）→ iVRF

標準 PQ-VRF が存在しないため、ハッシュベースの **indexed VRF (iVRF)** を採用（調査 `VRF-PQ-interim-options.md` 案2）。

### 2a. 暗号プリミティブ ✅ 完了・検証済み

固定深さ（2^16）Merkle 木。ハーベスタは 32B seed から葉 `leaf(i)=SHA3-256("catapult-ivrf-leaf"||seed||i_le64)` を
生成し、root をオンチェーン登録。ブロック index i では葉 i ＋認証パスを開示、
私的抽選値 = `SHA3-256(leaf_i || 親GenerationHash)`。安全性は SHA3-256 のみに依存（Shor 非該当）。

- C++ `crypto/iVrf.{h,cpp}`: `iVrfKeyTree`（root/prove）, `iVrfVerify`, `iVrfGenerationHash`。固定 **544B** proof。木構築 ~290ms/4MB。
- SDK `symbol/iVrf.js`: 同一実装。**C++⇄JS で root/leaf/path/generationHash が byte 単位一致**。
- gtest `iVrfTests.cpp` 追加。改竄/異index/範囲外/異seed 拒否、決定性、入力束縛を検証。

コミット: `478285e9b`（C++）, `5f55a75d`（SDK）

### 2b. コンセンサス統合 ⏳ 実装中

**採用方針: 高さ窓＋再登録（本格版）** — root に有効高さ範囲を付与し、窓満了で再登録。
`index = height − startHeight`。voting のエポック範囲イディオムに倣う。

**✅ コード完成・全ノードビルド成功**（server/broker/recovery/nemgen がクリーンにコンパイル）

実装済みステージ:
1. **config**（`0bbd076d5`）: `[chain] iVrfTreeDepth`(既定16) / `iVrfActivationDelay`(既定0) を
   オプショナル追加。proof は `iVrf_Max_Tree_Depth`(32)＝**固定1056B**確保で、config depth を
   変えてもブロックヘッダ形式は不変（先頭 depth 個のみ有効・残りゼロ詰め）。
2. **account state**（`7a1d317cd`）: VRF supplemental に root＋activation height を格納・直列化。
3. **observer**（`090af22cc`）: VrfKeyLink 確定時に activation height = link高さ + `iVrfActivationDelay` を記録。
4. **ブロックヘッダ wire**（`6830faff6`）: `crypto::VrfProof`→`crypto::iVrfProof`(1056B)。
5. **検証経路**（`6830faff6`）: BlockchainProcessor が root＋窓＋`index=height−activationHeight` で
   `iVrfVerify`→`iVrfGenerationHash`。NemesisBlockLoader は index0＋seed で検証。depth は
   `CreateBlockchainProcessor`/`DispatcherService` 経由で config から供給。
6. **harvester**（`6830faff6`）: vrf seed から `iVrfKeyTree` をアカウント毎にキャッシュ、
   登録 activation height を lookup、`prove(index)` で proof 生成。
7. **nemgen / mongo**（`c844a2417`）: nemesis proof を index0 で生成＋root をログ出力、
   mongo BlockMapper は iVRF proof(leaf＋path) を投影。

**残り:**
- SDK の block catbuffer モデル（`VrfProof`→iVRF proof、REST でのブロック読取用。二次的）。
- **nemesis 再生成＋2ノード実チェーン検証**（root 登録・ブロック生成継続・finalization 前進の確認）。

（旧・当初の残ステージ一覧は下記に保存）:

1. **account state**: VRF supplemental を「root(32B) ＋ startHeight」を保持する形へ拡張
   （現状は単一 `VrfPublicKey`）。`AccountPublicKeys` とその直列化（`AccountStateSerializer`）を変更。
2. **VrfKeyLink（coresystem プラグイン）**: link 確定時に observer が登録高さを記録し
   `startHeight = linkHeight + activationDelay`（grinding 対策の遅延）、`endHeight = startHeight + 2^Depth − 1`。
   root は既存の 32B linkedPublicKey を流用。窓・遅延の validator を追加。
3. **ブロックヘッダ wire**: `model/Block.h` の `crypto::VrfProof GenerationHashProof`(80B) →
   `crypto::iVrfProof`(544B)。SDK catbuffer の block model、mongo `BlockMapper` を追従。
   ※コンセンサス破壊変更＝**nemesis 再生成が必要**。
4. **harvester**: vrf seed からの `iVrfKeyTree` を**アカウント毎にキャッシュ**（毎試行の再構築 290ms は不可）。
   `index = height − startHeight` で `prove` して proof を格納。窓満了の再登録運用。
5. **検証経路**: `consumers/BlockchainProcessor.cpp` と `extensions/NemesisBlockLoader.cpp` の
   `VerifyVrfProof` を、登録 root・窓・`index=height−startHeight` を検査し
   `iVrfGenerationHash` を返す実装に置換。
6. **nemgen**: nemesis 署名者の iVRF root 登録＋nemesis ブロックの iVRF proof 生成。
7. **mongo / SDK / validators / tests**、その後 2ノードでファイナライズ＋ブロック生成の実チェーン検証。

**設計上の要点**
- 木キャッシュのメモリ（深さ16で4MB/アカウント）と起動コスト（~290ms）。深さは
  ブロック生成間隔と再登録頻度のトレードオフ。
- 登録→有効化の遅延で「当選する root を狙って登録する」grinding を防ぐ。
- proof フィールドは将来 LaV 等への差し替えに備え、深さ変更に追従できる設計とする。

---

## 現状まとめ
- ✅ Voting は完全に ML-DSA 化・検証済み。
- ✅ iVRF プリミティブは C++/SDK 実装・相互検証済み。
- ⏳ iVRF のコンセンサス統合（上記 2b）は設計確定済みで実装待ち。ここが残る最大の作業。
