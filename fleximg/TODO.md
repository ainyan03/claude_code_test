# fleximg TODO

## 構想段階

詳細は `docs/ideas/` を参照。

| アイデア | 概要 | 詳細 |
|---------|------|------|
| 画像デコーダーノード | JPEG/PNG等をRendererNode派生としてタイル処理 | [IDEA_IMAGE_DECODER_NODE.md](docs/ideas/IDEA_IMAGE_DECODER_NODE.md) |
| プッシュ型アフィン変換 | MCU単位プッシュ処理に対応するアフィン変換 | [IDEA_PUSH_MODE_AFFINE.md](docs/ideas/IDEA_PUSH_MODE_AFFINE.md) |
| アフィン変換の入力要求分割 | 回転時のAABB巨大化問題をY分割で解決 | [IDEA_AFFINE_REQUEST_SPLITTING.md](docs/ideas/IDEA_AFFINE_REQUEST_SPLITTING.md) |
| 上流ノードタイプマスク | ビットマスクで上流構成を伝播し最適化判断に活用 | [IDEA_UPSTREAM_NODE_TYPE_MASK.md](docs/ideas/IDEA_UPSTREAM_NODE_TYPE_MASK.md) |

## 実装予定

（なし）

## 既知の問題

（なし）
