# fleximg TODO

## 構想段階

詳細は `docs/ideas/` を参照。

| アイデア | 概要 | 詳細 |
|---------|------|------|
| 画像デコーダーノード | JPEG/PNG等をRendererNode派生としてタイル処理 | [IDEA_IMAGE_DECODER_NODE.md](docs/ideas/IDEA_IMAGE_DECODER_NODE.md) |
| アフィン変換の入力要求分割 | 回転時のAABB巨大化問題をY分割で解決 | [IDEA_AFFINE_REQUEST_SPLITTING.md](docs/ideas/IDEA_AFFINE_REQUEST_SPLITTING.md) |
| 上流ノードタイプマスク | ビットマスクで上流構成を伝播し最適化判断に活用 | [IDEA_UPSTREAM_NODE_TYPE_MASK.md](docs/ideas/IDEA_UPSTREAM_NODE_TYPE_MASK.md) |
| アフィン変換の透明非対応フォーマット問題 | RGB565等での範囲外不透明黒問題の対策 | [IDEA_AFFINE_ALPHA_HANDLING.md](docs/ideas/IDEA_AFFINE_ALPHA_HANDLING.md) |

## 実装済み

| 機能 | 概要 | バージョン |
|------|------|-----------|
| プッシュ型アフィン変換 | Renderer下流でのアフィン変換 | v2.19.0 |
| 固定小数点型基盤 | int_fixed8/int_fixed16、Point構造体、座標計算 | v2.5.0 |
| マイグレーションAPI削除 | Point2f, setOriginf() 等の削除 | v2.18.0 |
| doctestテスト環境 | 88テストケース、594アサーション | v2.20.0 |

## 実装予定

| 機能 | 概要 | 備考 |
|------|------|------|
| フィルタパラメータ固定小数点化 | brightness/alphaのパラメータ | 組み込み移植時 |

## 既知の問題

- アルファなしフォーマット（RGB332, RGB565, RGB888等）でアフィン変換すると、画像外エリアが不透明黒になる
  → 対策案は [IDEA_AFFINE_ALPHA_HANDLING.md](docs/ideas/IDEA_AFFINE_ALPHA_HANDLING.md) を参照

- レンダリング時に1pixelのドット抜けが規則的に縦に並んで発生する
  → 発生条件：スキャンラインレンダリング有効かつ上流側アフィン変換で縦横倍率3倍指定時
  → 発生を確認できた角度：131.0度、149.8度
