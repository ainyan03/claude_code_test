# fleximg TODO

## 構想段階

詳細は `docs/ideas/` を参照。

| アイデア | 概要 | 詳細 |
|---------|------|------|
| 画像デコーダーノード | JPEG/PNG等をRendererNode派生としてタイル処理 | [IDEA_IMAGE_DECODER_NODE.md](docs/ideas/IDEA_IMAGE_DECODER_NODE.md) |
| 上流ノードタイプマスク | ビットマスクで上流構成を伝播し最適化判断に活用 | [IDEA_UPSTREAM_NODE_TYPE_MASK.md](docs/ideas/IDEA_UPSTREAM_NODE_TYPE_MASK.md) |
| フォーマット交渉 | ノード間で最適なピクセルフォーマットを自動決定 | [IDEA_FORMAT_NEGOTIATION.md](docs/ideas/IDEA_FORMAT_NEGOTIATION.md) |
| ~~インデックスカラー~~ | ~~パレットインデックスフォーマット~~ | ~~[IDEA_INDEXED_COLOR_FORMATS.md](docs/ideas/IDEA_INDEXED_COLOR_FORMATS.md)~~ → 実装済み (v2.56.0) |
| Pipe埋め込み設計 | パイプライン接続構造の再設計 | [IDEA_PIPE_EMBEDDED_DESIGN.md](docs/ideas/IDEA_PIPE_EMBEDDED_DESIGN.md) |
| パイプラインV2 | パイプライン設計の刷新 | [IDEA_PIPELINE_V2.md](docs/ideas/IDEA_PIPELINE_V2.md) |
| スキャンラインDMA最適化 | DMA転送を活用した最適化 | [IDEA_SCANLINE_DMA_OPTIMIZATION.md](docs/ideas/IDEA_SCANLINE_DMA_OPTIMIZATION.md) |

## 実装済み

| 機能 | 概要 | バージョン |
|------|------|-----------|
| Node基底クラスTemplate Method化 | pullPrepare/pushPrepare等の共通処理を基底クラスでfinal化し、派生クラスはonXxxフックをオーバーライド | - |
| スキャンライン必須仕様 | パイプライン上のリクエストは必ずheight=1 | v2.30.0 |
| アフィン伝播最適化 | SourceNodeへのアフィン伝播、有効範囲のみ返却 | v2.30.0 |
| プッシュ型アフィン変換 | Renderer下流でのアフィン変換 | v2.19.0 |
| NinePatchSourceNode | Android 9patch互換の伸縮可能画像ソース | - |
| DAG禁止（木構造制限） | ノードグラフを木構造に制限し、DAGを禁止 | - |
| 固定小数点型基盤 | int_fixed8/int_fixed16、Point構造体、座標計算 | v2.5.0 |
| マイグレーションAPI削除 | Point2f, setOriginf() 等の削除 | v2.18.0 |
| doctestテスト環境 | 91テストケース | v2.20.0 |
| Grayscale8 / Index8 フォーマット | グレースケールとパレットインデックスフォーマット、ImageBufferパレットサポート | v2.56.0 |
| Bit-packed Index フォーマット | 1/2/4ビットパレットインデックス（MSB/LSB）、DDA・バイリニア補間対応 | v2.64.0 |
| AffineNodeシンプル化 | DEPRECATEDコード削除、行列保持・伝播のみに | - |
| NinePatchオーバーラップ対応 | パッチ区間のオーバーラップ設定でアフィン変換時の隙間を防止 | - |
| CompositeNode単一バッファ事前確保 | getDataRangeで合成範囲を事前計算し、単一バッファに直接ブレンド（N=32で27%改善） | v2.63.25 |
| Alpha8/Grayscale8 1chバイリニア補間 | 1chフォーマットのバイリニア補間を直接処理（RGBA8往復変換排除） | v2.63.28 |
| MatteNodeマスク範囲最適化 | マスク取得範囲をfg∪bg有効範囲に制限、bgフォールバック集約 | v2.63.28 |
| FormatConverter 変換パス事前解決 | resolveConverter で最適な変換関数を事前解決し分岐排除、thread_local 排除 | v2.57.0 |
| SinkNode toFormat 直接書き込み最適化 | FormatConverter による直接変換書き込み（中間バッファ排除） | v2.57.0 |

## 実装予定

| 機能 | 概要 | 備考 |
|------|------|------|
| VerticalBlurNodeキャッシュ幅最適化 | 上流AABBに基づくキャッシュ幅の最適化 | 座標オフセット管理の見直しが必要 |
| フィルタパラメータ固定小数点化 | brightness/alphaのパラメータ | 組み込み移植時 |

## 既知の問題

なし
