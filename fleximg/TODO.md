# fleximg TODO

## 構想段階

詳細は `docs/ideas/` を参照。

| アイデア | 概要 | 詳細 |
|---------|------|------|
| 画像デコーダーノード | JPEG/PNG等をRendererNode派生としてタイル処理 | [IDEA_IMAGE_DECODER_NODE.md](docs/ideas/IDEA_IMAGE_DECODER_NODE.md) |
| 上流ノードタイプマスク | ビットマスクで上流構成を伝播し最適化判断に活用 | [IDEA_UPSTREAM_NODE_TYPE_MASK.md](docs/ideas/IDEA_UPSTREAM_NODE_TYPE_MASK.md) |

## 実装済み

| 機能 | 概要 | バージョン |
|------|------|-----------|
| Node基底クラスTemplate Method化 | pullPrepare/pushPrepare等の共通処理を基底クラスでfinal化し、派生クラスはonXxxフックをオーバーライド | - |
| スキャンライン必須仕様 | パイプライン上のリクエストは必ずheight=1 | v2.30.0 |
| アフィン伝播最適化 | SourceNodeへのアフィン伝播、有効範囲のみ返却 | v2.30.0 |
| プッシュ型アフィン変換 | Renderer下流でのアフィン変換 | v2.19.0 |
| NinePatchSourceNode | Android 9patch互換の伸縮可能画像ソース | - |
| 固定小数点型基盤 | int_fixed8/int_fixed16、Point構造体、座標計算 | v2.5.0 |
| マイグレーションAPI削除 | Point2f, setOriginf() 等の削除 | v2.18.0 |
| doctestテスト環境 | 91テストケース | v2.20.0 |
| AffineNodeシンプル化 | DEPRECATEDコード削除、行列保持・伝播のみに | - |

## 実装予定

| 機能 | 概要 | 備考 |
|------|------|------|
| フィルタパラメータ固定小数点化 | brightness/alphaのパラメータ | 組み込み移植時 |

## 既知の問題

| 問題 | 概要 | 備考 |
|------|------|------|
| NinePatch回転時のドット抜け | 下流にAffineNodeを置いて回転させると区画継ぎ目にドット抜けが発生 | スキャンライン境界の丸め誤差と思われる |
