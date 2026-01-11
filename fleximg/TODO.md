# fleximg TODO

## 構想段階

詳細は `docs/ideas/` を参照。

| アイデア | 概要 | 詳細 |
|---------|------|------|
| 画像デコーダーノード | JPEG/PNG等をRendererNode派生としてタイル処理 | [IDEA_IMAGE_DECODER_NODE.md](docs/ideas/IDEA_IMAGE_DECODER_NODE.md) |
| アフィン変換の入力要求分割 | 回転時のAABB巨大化問題をY分割で解決 | [IDEA_AFFINE_REQUEST_SPLITTING.md](docs/ideas/IDEA_AFFINE_REQUEST_SPLITTING.md) |
| 上流ノードタイプマスク | ビットマスクで上流構成を伝播し最適化判断に活用 | [IDEA_UPSTREAM_NODE_TYPE_MASK.md](docs/ideas/IDEA_UPSTREAM_NODE_TYPE_MASK.md) |

## 実装済み

| 機能 | 概要 | バージョン |
|------|------|-----------|
| プッシュ型アフィン変換 | Renderer下流でのアフィン変換 | v2.19.0 |

## 実装予定

（なし）

## 既知の問題

・アフィン変換で拡大方向の変換をすると画像に隙間ができることがある（要AABBマージン再検証）
・アルファなしフォーマット（RGB332, RGB565, RGB888等）でアフィン変換すると、画像外エリアが不透明黒（0）になる
  - 原因: アルファチャンネルがないため「透明」を表現できない
  - 対策案: (1) 合成前にRGBA8変換 (2) カラーキー方式 (3) 背景色指定
