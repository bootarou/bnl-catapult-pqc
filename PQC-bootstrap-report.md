# PQC symbol-bootstrap 対応レポート

**対象**: BNL — Post-Quantum Catapult（Symbol/catapult の非公式・実験的フォーク。公式 Symbol/NEM とは無関係）
**日付**: 2026-07-10
**目的**: `symbol-bootstrap` の**操作方法を変えずに**、PQC チェーン（ML-DSA-44 署名 / ML-KEM-768 暗号 / iVRF ブロック抽選 / ML-DSA ファイナリティ投票）の基本的なカスタムネットワーク運用を可能にする。

---

## 1. 結論

`symbol-bootstrap` の標準コマンド（`config` → `compose` → `up`）のみで、PQC チェーンのブート・採掘・API 提供までフルスタックで動作することを確認した。

```
symbol-bootstrap config  -p bootstrap -a dual   # ML-DSA 鍵・証明書・iVRF VrfKeyLink・PQC nemesis 生成
symbol-bootstrap compose --upgrade              # docker-compose.yml 生成
docker compose up -d                            # node + broker + mongo + rest 起動
```

- **チェーン高さ**: 継続採掘中（検証時点で 155 まで到達、エラー 0）
- **署名**: 全ブロックが ML-DSA-44 署名鍵で採掘
- **VRF**: iVRF（ハッシュベース indexed VRF）でブロック抽選、`invalid vrf public key` エラーなし
- **REST**: `/blocks/2` が `iVrfProofLeaf` + `iVrfProofPath` を返し、旧 ECVRF の `proofGamma` は消失

---

## 2. 本セッションで解消した運用上の課題

### 課題 A: mongo (`db`) が起動直後に exit 100 で停止
**症状**: `IllegalOperation: Attempted to create a lock file on a read-only directory: /dbdata`

**原因**: 公式 `mongo:5.0.15` の entrypoint は、デフォルトのデータパス（`/data/db`）のみを `mongodb` ユーザーに chown してから権限を drop する。bootstrap はカスタムパス `/dbdata` を使っていたため、root 所有のまま `mongodb`(uid 999) に落ち、lock ファイルを作成できなかった。

**修正** (`lib/service/ComposeService.js`):
- データディレクトリのマウント先を `/dbdata` → `/data/db` に変更（entrypoint が chown する経路に乗せる）
- db サービスの `user` オーバーライドを撤去し、entrypoint が root で chown → 権限 drop できるようにした

**結果**: db が `Waiting for connections` まで正常起動。

### 課題 B: node がブロックを採掘しない（`invalid vrf public key`）
**症状**: `UnlockedAccountsUpdater` が全 harvester を「invalid vrf public key」で拒否。

**原因**: 登録されている VRF 公開鍵は iVRF Merkle ルートだが、鍵ペアからの公開鍵導出が旧 ed25519 のままで一致しなかった。

**修正** (catapult `src/catapult/crypto/KeyPair.cpp`, commit `a5c1c36`):
- `VrfKeyPairTraits::ExtractPublicKeyFromPrivateKey` を、シードから iVRF ツリー（`iVrf_Default_Tree_Depth = 16`）を構築し**ルートを VRF 公開鍵として返す**よう変更
- リリースバイナリを再ビルドし、`symbolplatform/symbol-server:gcc-1.0.3.9-bnl` イメージにオーバーレイ

**結果**: node が連続採掘（ブロック 9 → 155…、vrf エラー 0）。

### 課題 C: broker が起動時に stale lock で crash (exit 253)
**症状**: `could not acquire instance lock "./data/recovery.lock"`（node と同時 `--force-recreate` した際のロック競合）

**原因**: node/broker 同時再作成による一時的なロック競合。恒久的な不具合ではなく起動順序の問題。

**対処**: `docker compose down` → stale lock 掃除 → `docker compose up -d` のクリーンサイクルで解消（compose の `depends_on` が順序を保証）。

### 課題 D: REST が起動直後に停止（`plugin 'undefined' not supported by route system`）
**症状**: `routeSystem.js:78` で `plugin 'undefined'` エラー。

**原因**: PQC REST は 2.5.1 系ソースからビルドされており `routeExtensions` を要求するが、bootstrap 1.1.10 が生成する `rest.json` に同フィールドが無い。`[].concat(extensions, routeExtensions)` が末尾に `undefined` を注入していた。

**修正** (`config/rest-gateway/rest.json.mustache`):
- `"routeExtensions": []` を追加（稼働中の生成済み `rest.json` も同様にパッチ）

**結果**: REST が `listening on port 3000` で正常起動。

---

## 3. フルスタック検証結果

| 検証項目 | コマンド / 確認 | 結果 |
|---|---|---|
| 鍵・nemesis 生成 | `config -p bootstrap -a dual` | ML-DSA 鍵・証明書・iVRF VrfKeyLink・PQC nemesis 生成 ✓ |
| コンテナ起動 | `compose --upgrade` + `docker compose up -d` | node / broker / db / rest すべて healthy ✓ |
| iVRF 採掘 | node ログ | ブロック 9→155 連続採掘、ML-DSA 署名、vrf エラー 0 ✓ |
| broker → mongo 投影 | REST が mongo からブロック提供 | 投影動作 ✓ |
| REST iVRF ブロック | `GET /blocks/2` | `iVrfProofLeaf`(32B) + `iVrfProofPath`(2048hex, depth-16 有効 + zero-pad)、`proofGamma` 無し ✓ |
| REST チェーン情報 | `GET /chain/info` | 高さ増加（155）✓ |

---

## 4. 変更ファイル一覧

### symbol-bootstrap（branch `pqc-bootstrap`）
本セッションのコミット **`83e6e2e`**（author: `zeromax-star`）:
- `lib/service/ComposeService.js` — db マウント `/data/db` 化・db user 撤去（課題 A）
- `config/rest-gateway/rest.json.mustache` — `routeExtensions: []` 追加（課題 D）
- `presets/shared.yml` — `symbolServerImage`/`symbolRestImage` を PQC イメージへ

前セッションまでの PQC 対応コミット（参考）:
- `7b57f04` pqc: generate PQC (ML-DSA-44 + iVRF) networks with unchanged CLI
- `4088150` presets: use the PQC catapult-server image
- `a0d580e` certificates: generate ML-DSA-44 (FIPS 204) keys/certs

### catapult（client/catapult）
- `a5c1c36` iVrf: derive the vrf public key as the iVRF tree root（課題 B）

---

## 5. 使用イメージ

| コンポーネント | イメージ |
|---|---|
| catapult server / broker | `symbolplatform/symbol-server:gcc-1.0.3.9-bnl`（ML-DSA-44 + iVRF + ML-DSA voting） |
| REST gateway | `symbolplatform/symbol-rest:2.4.3-bnl`（iVRF ブロックスキーマ対応） |
| mongo | `mongo:5.0.15`（標準） |

---

## 6. 補足・注意事項

- **プッシュ未実施**: 認証情報（PAT）は削除済みのため、`pqc-bootstrap` ブランチのコミットはローカル止まり。プッシュはユーザー側で実施。
- **チェーン再生成時の注意**: `config --reset` は鍵・nemesis を再生成するため、稼働中チェーンの state と齟齬が出る。既存チェーンを維持する場合は `compose` のみ再生成し、生成済み設定をパッチする運用が安全（本セッションで REST 設定に適用）。
- **単一ノード時の警告**: `no packet io available for ...` はピア不在の単一ノード構成による無害な警告で、採掘・API には影響しない。
