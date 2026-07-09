# JavaScript SDK — ML-DSA-44 (FIPS 204) 対応レポート

対象: `symbol/sdk/javascript`
検証日: 2026-07-09

## 目的
新チェーン（ポスト量子 catapult）のコア署名を ed25519 から ML-DSA-44 に置き換えたことに合わせ、
JS SDK のトランザクション署名・検証・直列化・アドレス導出を ML-DSA-44 へ追従させる。
VRF・voting は従来どおり ed25519（32B）のまま温存。

## 暗号パラメータ
| 項目 | ed25519(旧) | ML-DSA-44(新) |
|------|-------------|----------------|
| PublicKey | 32 B | **1312 B** |
| Signature | 64 B | **2420 B** |
| PrivateKey | 32 B | 32 B（ML-DSA seed。鍵展開は決定的） |
| VRF PublicKey | 32 B | 32 B（据え置き） |
| VotingPublicKey | 32 B | 32 B（据え置き） |

## 変更ファイル
1. **`src/CryptoTypes.js`** — `PublicKey.SIZE` 32→1312、`Signature.SIZE` 64→2420。
   `PrivateKey.SIZE`(32) と `SharedKey256.SIZE`(32) は据え置き。
2. **`src/symbol/KeyPair.js`** — `@noble/post-quantum` の `ml_dsa44` を使用。
   - `keygen(seed32)` で鍵対を決定的に生成、`publicKey` は 1312B。
   - `sign(secretKey, message)` / `verify(publicKey, message, signature)`。
3. **`src/symbol/models.js`**（catbuffer 生成モデル）
   - 生成モデル内の独自 `PublicKey` を 32→1312、`Signature` を 64→2420 に更新。
   - 32B の `VrfPublicKey` クラスを追加し、`VrfKeyLinkTransactionV1` /
     `EmbeddedVrfKeyLinkTransactionV1` の `linkedPublicKey` をこれに再ポイント
     （C++ スキーマ `VrfKeyLinkTransactionBody<..., VrfPublicKey, ...>` に一致）。
   - `AccountKeyLink` / `NodeKeyLink` の `linkedPublicKey` は account `Key`＝1312B のまま。
   - `VotingPublicKey`(32B) は据え置き。
4. **`src/utils/converter.js`** — `bytesToInt` / `bytesToBigInt` に非整列フォールバックを追加。
   ML-DSA 署名(2420B) がトランザクション本体の 8B 境界を崩すため、
   読み出しオフセットが整列していない場合はアライメント済みバッファへコピーしてから読む。
5. **`package.json`** — `@noble/post-quantum` を `0.4.1` に固定
   （C++/OpenSSL・poc・jssign と相互検証済みのバージョン。0.5.x は `sign` の引数順が逆）。

## 署名・ハッシュ方式（catapult と一致）
- 署名対象 payload = `generationHashSeed(32) || tx[Header_Size..end]`
- `Header_Size = 4(Size)+4(reserved1)+2420(Sig)+1312(Key)+4(reserved2) = 3744`
- entity hash = `SHA3-256(Signature || SignerPublicKey || generationHashSeed || dataBuffer)`
- いずれも `Signature.SIZE` / `PublicKey.SIZE` を参照するため自動追従。

## 検証結果（node:22, HARVESTER_A 実鍵）
- `KeyPair.publicKey` == catapult 生成の公開鍵（1312B）: **一致**
- `sign`/`verify` 往復: **OK**（署名長 2420B、改竄検出 OK）
- アドレス導出 `Network.TESTNET.publicKeyToAddress`
  == catapult アドレス `TC7C7X6K...QN7Y`: **一致**
- `SymbolFacade` で TransferTransaction 生成→署名→直列化:
  - 直列化された Signature/Signer フィールドが一致
  - `facade.verifyTransaction`: **true**
  - 直列化→`deserialize`→再直列化が完全一致（fee/deadline も復元）

## 既知の制約（未対応）
- **`src/symbol/MessageEncoder.js` / `src/symbol/SharedKey.js`（暗号化メッセージ）は未対応。**
  X25519(tweetnacl) ECDH を使っており、account 公開鍵が ML-DSA(1312B) になったため
  そのままでは動作しない。ポスト量子化には ML-KEM-768（C++ `crypto/SharedKey.cpp` と同様）
  ベースの設計が必要だが、account へ ML-KEM 公開鍵を公開する仕組みが現状のスキーマに無く、
  暗号化メッセージ用の相手鍵の入手経路が未定義。今回は署名系を優先し、本機能は別途設計とする。
- 既存のユニットテスト／テストベクタ（`test/`, `vectors/`）は ed25519 前提のため未更新。
  ランタイム動作は上記 e2e で確認済み。
