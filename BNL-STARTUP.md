# BNL Post-Quantum Catapult — 起動マニュアル

ML-DSA-44（署名）＋ iVRF（ブロック抽選）＋ ML-KEM-768（鍵交換）の PQC 版 catapult を、
ビルドから**フルスタック（node ＋ broker ＋ mongo ＋ REST）**まで起動・検証する手順。

> ⚠️ これは Symbol 公式とは無関係の**非公式な実験フォーク**です。公開 Symbol ネットワークには参加できません（新 nemesis 必須）。

---

## 0. 前提

- Docker が使えること（WSL2 / Linux）。
- 想定レイアウト（親ディレクトリを `$ROOT` とする。例: `/home/user/catapult`）:
  - `$ROOT/symbol/` … 本リポジトリ（catapult-server ＋ REST ＋ SDK）
  - `$ROOT/net/` … 運用成果物（鍵・証明書・config・nemesis seed・各ノードの data）
  - `$ROOT/jssign/` … PQC トランザクション生成ツール（`vrflink.mjs` 等、`npm install` 済み）
  - `$ROOT/cache/` `$ROOT/out/` … ビルドキャッシュ／成果物
- ビルドイメージ: `symbolplatform/symbol-server-build-base:ubuntu-gcc-15-skylake`（OpenSSL 3.5+、ML-DSA/ML-KEM ネイティブ）。

主要パラメータ（本手順の既定値）:

| 項目 | 値 |
|---|---|
| catapult-server イメージ | `symbolplatform/symbol-server:gcc-1.0.3.9-bnl` |
| `iVrfTreeDepth`（`[chain]`） | `16`（1登録=65,536ブロック） |
| `fileDatabaseBatchSize`（`[node]`） | `1`（nemgen seed の `00001.dat` 形式に一致） |
| `nemesisSignerVrfPublicKey`（`[network]`） | nemesis vrf 秘密鍵の **iVRF root** |
| finalization/voting | 本手順では無効（iVRF に集中。voting はライブラリ検証済み） |

---

## 1. PQC バイナリ／イメージのビルド

### 1-1. 個別ターゲット（開発時）
```bash
cd $ROOT
./build-target.sh catapult.server catapult.broker catapult.tools.nemgen   # 任意のターゲット
```
成果物は `cache/build/`（RelWithDebInfo）。ユニット検証やスタンドアロン確認に使用。

### 1-2. リリースバイナリ ＋ 実行イメージ
```bash
cd $ROOT
rm -rf out/binaries
./build-bnl-image.sh symbolplatform/symbol-server:gcc-1.0.3.9-bnl   # [1/3] release compile -> out/binaries
```
`[1/3]` のコンパイルが完了すれば `out/binaries/{bin,lib,deps}` が生成される
（`bin`=実行ファイル、`lib`=**プラグイン .so ＋ 共有ライブラリ**、`deps`=OpenSSL 等）。

`[2/3]` のイメージ整形スクリプトが権限で失敗する環境では、**自前 Dockerfile で重ねる**:
```bash
cat > out/Dockerfile.bnl <<'EOF'
FROM nftdrive/bnl-catapult-server:1.0.3.9-cf1
COPY bin/ /usr/catapult/bin/
COPY lib/ /usr/catapult/lib/
COPY deps/ /usr/catapult/deps/
EOF
docker build -f out/Dockerfile.bnl -t symbolplatform/symbol-server:gcc-1.0.3.9-bnl out/binaries
```
> プラグインは release ビルドの一貫したセット（`/usr/catapult/lib`）を使うこと。RelWithDebInfo の
> プラグインを個別に混載すると `sub cache has already been registered with id ...` で落ちる。

---

## 2. 暗号成果物の準備（既に `net/` にある場合はスキップ可）

### 2-1. iVRF root の算出（SDK）
vrf 秘密鍵（32B seed）から登録用の iVRF root を計算:
```bash
cd $ROOT/symbol/sdk/javascript
docker run --rm -v $PWD:/app -w /app -e VRF=<vrf秘密鍵hex> node:22-slim node -e '
  const { iVrfKeyTree } = await import("./src/symbol/iVrf.js");
  const t = new iVrfKeyTree(Uint8Array.from(Buffer.from(process.env.VRF,"hex")), 16);
  console.log(Buffer.from(t.root).toString("hex").toUpperCase());
'
```

### 2-2. ネットワーク config
`net/*/resources/config-network.properties`:
- `nemesisSignerVrfPublicKey = <nemesis vrf 秘密鍵の iVRF root>`
- `[chain]` に `iVrfTreeDepth = 16`

`net/*/resources/config-node.properties`:
- `fileDatabaseBatchSize = 1`

`net/*/resources/config-user.properties`:
- `pluginsDirectory = /usr/catapult/lib`

### 2-3. VrfKeyLink（ハーベスタの iVRF root 登録）
ハーベスタ口座が自分の iVRF root を nemesis で登録する。`jssign/vrflink.mjs` で ML-DSA 署名:
```bash
cd $ROOT/jssign
docker run --rm -v $PWD:/app -w /app node:22-slim \
  node vrflink.mjs <ハーベスタ署名秘密鍵> <ハーベスタiVRF_root> <generationHashSeed> 1 0 \
  | python3 -c 'import sys;open("/dev/stdout","wb")' >/dev/null   # 出力hexを .bin へ
# 実際は hex を bytes.fromhex で net/nemgen/txes/vrf_<node>.bin に書き出す
```
（`nemesisSignerVrfPublicKey` と各ハーベスタの `vrf_*.bin` の root が、nemgen ログの
`nemesis iVRF root` と一致すること。）

---

## 3. nemesis 生成

PQC イメージの nemgen を使う（プラグインは `/usr/catapult/lib` に同梱）:
```bash
cd $ROOT
rm -rf net/_nemgen_work && mkdir -p net/_nemgen_work net/seed
docker run --rm --user="$(id -u):$(id -g)" \
  --volume="$PWD/net:/net" \
  --volume="$PWD/net/_nemgen_work:/node" \
  symbolplatform/symbol-server:gcc-1.0.3.9-bnl \
  /usr/catapult/bin/catapult.tools.nemgen --resources /net/node-a -p /net/nemgen/nemesis.properties -t
```
ログに `nemesis iVRF root (set network NemesisSignerVrfPublicKey to this): <root>` と
`Generation Hash: <...>` が出れば成功。seed は `binDirectory`（`net/seed/`）に出力される。

### seed を各ノードへ配布
```bash
cd $ROOT
for n in node-a node-b node-api node-api-broker; do
  cp -f net/seed/00000/00001.dat net/seed/00000/hashes.dat net/$n/seed/00000/ 2>/dev/null
  rm -rf net/$n/data && mkdir -p net/$n/data && cp -r net/seed/* net/$n/data/ 2>/dev/null
done
```

---

## 4. フルスタック起動（推奨）

`$ROOT/docker-compose.pqc.yml`（本リポジトリ同梱の `symbol/docker-compose.pqc.yml` を `$ROOT` に配置）で
**mongo ＋ node-api（iVRF harvest＋API）＋ broker ＋ REST** を一括起動:

```bash
cd $ROOT
# node-api を harvest 有効に（API 単体で harvest ＋ 配信させる場合）
sed -i 's/^enableAutoHarvesting = .*/enableAutoHarvesting = true/' net/node-api/resources/config-harvesting.properties

docker compose -f docker-compose.pqc.yml up -d
```

停止・破棄:
```bash
docker compose -f docker-compose.pqc.yml down
```

---

## 5. 動作確認（REST）

```bash
# チェーンが伸びているか
curl -s http://localhost:3000/chain/info | python3 -m json.tool

# ブロックの iVRF proof（旧 proofGamma は無く、iVrfProofLeaf / iVrfProofPath が返る）
curl -s http://localhost:3000/blocks/2 | python3 -c '
import json,sys; b=json.load(sys.stdin)["block"]
print("height:", b["height"])
print("iVrfProofLeaf:", b["iVrfProofLeaf"])
print("iVrfProofPath hex len:", len(b["iVrfProofPath"]), "(=2048)")'
```
`iVrfProofLeaf` が SDK の `computeLeaf(vrf秘密鍵, height-activationHeight)` と一致する
（= ノードの iVRF 抽選と SDK 計算がバイト一致）。

mongo を直接見る場合:
```bash
docker compose -f docker-compose.pqc.yml exec -T mongo \
  mongosh --quiet catapult --eval 'db.blocks.countDocuments()'
```

---

## 6. （任意）2ノードでの iVRF ブロック生成・同期検証

```bash
cd $ROOT
docker network create bnlnet 2>/dev/null || true
for n in node-a node-b; do find net/$n -name server.lock -delete; done

docker run -d --name node-a --network bnlnet -v "$PWD/net/node-a:/node" \
  symbolplatform/symbol-server:gcc-1.0.3.9-bnl /usr/catapult/bin/catapult.server /node
docker run -d --name node-b --network bnlnet -v "$PWD/net/node-b:/node" \
  symbolplatform/symbol-server:gcc-1.0.3.9-bnl /usr/catapult/bin/catapult.server /node

# node-a が高さ 2,3,... を harvest、node-b が Remote_Pull で同期
docker logs node-a 2>&1 | grep "harvested block at" | tail
docker logs node-b 2>&1 | grep "comparing chain scores" | tail -1   # local == remote なら同期成功
```

---

## 7. トラブルシューティング

| 症状 | 原因 / 対処 |
|---|---|
| `sub cache has already been registered with id N` | RelWithDebInfo プラグインの混載。release イメージの `/usr/catapult/lib` を使う。 |
| `couldn't open .../00000/00000.dat` | seed は `00001.dat`（batch=1）。`fileDatabaseBatchSize = 1` に合わせる。 |
| `create_directories: /usr/catapult/logs Permission denied` | コンテナを root で実行（compose は `user: root`）。 |
| ノードが harvest しない | API ノードは既定 `enableAutoHarvesting = false`。harvest させるなら `true`。 |
| `nemesis block has invalid generation hash proof` | `nemesisSignerVrfPublicKey` が nemgen の iVRF root と不一致。§2-1/§3 で再確認。 |
| REST が旧 `proofGamma` を返す/鍵長エラー | REST の `symbol-sdk` が公開版のまま。`client/rest` で `npm install`（`package.json` は local SDK を `file:` 参照）。 |
| 起動直後に `server.lock` で失敗 | 前プロセスの lock 残り。`find net -name server.lock -delete`。 |

---

## 参考

- 実装・検証の詳細: [`PQC-VRF-voting-report.md`](PQC-VRF-voting-report.md)（voting=ML-DSA / VRF=iVRF）、
  [`ML-DSA-44-sdk-report.md`](ML-DSA-44-sdk-report.md)（SDK 署名・ML-KEM メッセージ暗号化）。
- symbol-bootstrap は暗号層が ed25519 SDK 2.x 依存のため自前鍵/nemesis は非PQC。フル PQC スタックは本手順の
  docker-compose を使用（`presets/shared.yml` の server イメージは PQC 版に更新済み）。
