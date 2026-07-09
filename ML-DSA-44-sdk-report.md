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

## メッセージ暗号化（ML-KEM-768 / FIPS 203）
X25519(tweetnacl) ECDH を **ML-KEM-768 の鍵カプセル化(KEM)** に置換。
C++ ノード（`crypto/SharedKey.cpp`）と同一構成で、委譲収穫の相互運用も可能。

**変更ファイル**
- **`src/symbol/SharedKey.js`** — ML-KEM ヘルパに全面書き換え:
  - `deriveMlKemKeyPair(privateKey)` / `deriveMlKemPublicKey(privateKey)`
    — account 秘密鍵(32B seed) から ML-KEM 鍵対を決定的に導出。
    seed = `SHA512("catapult-mlkem-seed"(19B) || privateKey(32B))[:64]` を
    `ml_kem768.keygen` に投入（C++ `DeriveMlKemSeed` と同一）。
  - `encapsulateSharedKey(recipientMlKemPublicKey)` → `{ cipherText(1088B), sharedKey(32B) }`
  - `decapsulateSharedKey(privateKey, cipherText)` → `sharedKey(32B)`
  - HKDF = `HKDF-SHA256(salt=zero32, IKM=sharedSecret, info="catapult", L=32)`
    （C++ `Hkdf_Hmac_Sha256_32` と一致）。
- **`src/symbol/MessageEncoder.js`** — KEM ベースに書き換え。
  KEM は DH と非対称なので暗号文を同送する:
  - `encode(recipientMlKemPublicKey, message)`
    → `[0x01][cipherText 1088][tag 16][iv 12][AES-GCM 暗号文]`
  - `tryDecode(_, encoded)` — 自身の秘密鍵で decapsulate → AES-GCM 復号
    （相手公開鍵は不要になったため第1引数は互換のため残置・未使用）。
  - `encodePersistentHarvestingDelegation(nodeMlKemPublicKey, remote, vrf)`
    → `[DELEGATION_MARKER 8][cipherText 1088][tag][iv][暗号文]`
  - `get mlKemPublicKey()` を追加（相手に渡す自分の ML-KEM 公開鍵）。
- **`src/impl/CipherHelpers.js`** — 事前計算した鍵で暗復号する
  `encodeAesGcmWithKey` / `decodeAesGcmWithKey` を追加。
- **`src/facade/SymbolFacade.js`** — 旧 `static deriveSharedKey`（DH）を廃し、
  `deriveMlKemPublicKey` / `deriveMlKemKeyPair` / `encapsulateSharedKey` /
  `decapsulateSharedKey` を静的公開。

**運用上の注意**
- ML-KEM 公開鍵は ML-DSA account 公開鍵から導出できない（別プリミティブ）。
  送信者は受信者の **ML-KEM 公開鍵(1184B)** を別途入手する必要がある
  （受信者が `deriveMlKemPublicKey` / `encoder.mlKemPublicKey` で提示）。
- NEM 側（`src/nem/*`, `src/SharedKey.js` の X25519）は対象外・従来のまま。

**検証結果（OpenSSL 3.5.3 / noble 相互運用）**
- seed→ML-KEM 公開鍵: noble == OpenSSL（SHA256 一致、byte 単位一致）
- noble `encapsulate` → OpenSSL `pkeyutl -decap`: shared secret **一致**
- SDK 内 e2e: 暗号化メッセージ往復（日本語/絵文字含む）一致、
  誤鍵拒否、AES-GCM 改竄検出、委譲収穫（remote+vrf 秘密鍵）往復一致

## 既知の制約
- 既存のユニットテスト／テストベクタ（`test/`, `vectors/`）は ed25519 前提のため未更新。
  ランタイム動作は上記 e2e で確認済み。
