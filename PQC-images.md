# PQC Docker イメージ一覧（レジストリ プッシュ準備）

**対象**: BNL — Post-Quantum Catapult（Symbol/catapult の非公式・実験的フォーク。公式 Symbol/NEM とは無関係）
**日付**: 2026-07-10
**アーキテクチャ**: すべて `linux/amd64`（現状 arm64 ビルドなし）

---

## 1. プッシュ対象イメージ（PQC ランタイム）

再タグ済み。名前空間は `nftdrive`。

| # | プッシュ用タグ（確定） | 元のローカルタグ | Image ID | サイズ | 役割 | ベースイメージ |
|---|---|---|---|---|---|---|
| 1 | `nftdrive/bnl-catapult-server-pqc:1.0.3.9-bnl` | `symbolplatform/symbol-server:gcc-1.0.3.9-bnl` | `620b960636ed` | 3.95 GB | catapult server + broker（ML-DSA-44 署名 / ML-KEM-768 / iVRF 抽選 / ML-DSA 投票） | `nftdrive/bnl-catapult-server:1.0.3.9-cf1` にリリースバイナリをオーバーレイ |
| 2 | `nftdrive/bnl-catapult-rest-pqc:2.4.3-bnl` | `symbolplatform/symbol-rest:2.4.3-bnl` | `570ee2e29bc3` | 726 MB | REST ゲートウェイ（iVRF ブロックスキーマ対応、内部バージョン 2.5.1） | `client/rest` ソースからビルド |

> **mongo は対象外**: `mongo:5.0.15`（Docker 公式・無改変）をそのまま使用するためプッシュ不要。

---

## 2. 名前空間について

元のタグは公式 Symbol の Docker Hub 名前空間 `symbolplatform/` を使用していたため、
非公式フォークとして誤解を避ける目的で `nftdrive` 名前空間へ再タグ済み（セクション 1 の表）。
`symbolplatform/` のままではプッシュ権限もない。

`presets/shared.yml` の参照も新タグへ更新済み:
```yaml
symbolServerImage: nftdrive/bnl-catapult-server-pqc:1.0.3.9-bnl
symbolRestImage:   nftdrive/bnl-catapult-rest-pqc:2.4.3-bnl
```

---

## 3. プッシュ手順

再タグは適用済みなので、ログインしてプッシュするだけ。

```bash
# ログイン（nftdrive アカウントの認証情報で）
docker login

# プッシュ
docker push nftdrive/bnl-catapult-server-pqc:1.0.3.9-bnl
docker push nftdrive/bnl-catapult-rest-pqc:2.4.3-bnl
```

> 認証情報（PAT）は本環境から削除済みのため、`docker login` とプッシュはユーザー側で実施すること。

---

## 4. 参考: ローカルに存在する関連イメージ（プッシュ不要）

以下はビルド中間物・ベースイメージであり、通常はレジストリに公開する必要はない。

| イメージ | 用途 |
|---|---|
| `nftdrive/bnl-catapult-server:1.0.3.9-cf1` (`bc2c27c3c2a3`) | server イメージのベース（オーバーレイ元） |
| `catapult-server-bnl:local` / `catapult-server-bnl-patched:local` | ビルド中間物 |
| `nftdrive-bnl-catapult-server-patched:1.0.3.9-cf1` | 同上 |
| `symbolplatform/symbol-server-build-base:ubuntu-gcc-15-skylake` (14.4 GB) | catapult ビルド用ベース（ソースからビルドする場合のみ必要） |
| `symbol-server-patched:gcc-1.0.3.9` | 旧パッチ版 |

---

## 5. 補足・今後の検討事項

- **マルチアーキテクチャ**: 現状 `linux/amd64` のみ。Apple Silicon / arm64 サーバー対応が必要なら `docker buildx` でマルチアーチビルド・`--push` によるマニフェスト作成を検討。
- **タグ運用**: `1.0.3.9-bnl` のような固定バージョンタグに加え、`latest` を併用するかは運用方針次第。イミュータブルなバージョンタグ推奨。
- **サイズ削減**: server イメージ 3.95 GB は大きめ。デバッグシンボルや不要な静的ライブラリ除去で削減余地あり（別タスク）。
- **ライセンス／帰属**: 公開時は README の非公式フォーク免責を同梱し、`symbolplatform`・`nftdrive` 等の他者名前空間を最終公開タグに含めないこと。
