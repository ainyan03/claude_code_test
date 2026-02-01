# 設計ドキュメント

fleximg の設計ドキュメント一覧です。

## 概要

| ドキュメント | 内容 |
|-------------|------|
| [QUICKSTART.md](QUICKSTART.md) | クイックスタートガイド |
| [CONCEPTS.md](CONCEPTS.md) | 基本概念（初見の方向け） |
| [ARCHITECTURE.md](ARCHITECTURE.md) | 全体アーキテクチャ |
| [TROUBLESHOOTING.md](TROUBLESHOOTING.md) | よくある問題と解決方法 |

## 詳細設計

| ドキュメント | 対象 |
|-------------|------|
| [DESIGN_RENDERER_NODE.md](DESIGN_RENDERER_NODE.md) | RendererNode の Pull/Push 処理フロー |
| [DESIGN_FILTER_NODES.md](DESIGN_FILTER_NODES.md) | フィルタノードの階層構造 |
| [DESIGN_TYPE_STRUCTURE.md](DESIGN_TYPE_STRUCTURE.md) | ViewPort/ImageBuffer/RenderResponse の設計 |
| [DESIGN_PIXEL_FORMAT.md](DESIGN_PIXEL_FORMAT.md) | ピクセルフォーマット変換アルゴリズム |
| [DESIGN_CHANNEL_DESCRIPTOR.md](DESIGN_CHANNEL_DESCRIPTOR.md) | ChannelType・Alpha8フォーマット設計 |
| [DESIGN_PERF_METRICS.md](DESIGN_PERF_METRICS.md) | パフォーマンス計測基盤 |
| [DESIGN_IMAGE_BUFFER_SET.md](DESIGN_IMAGE_BUFFER_SET.md) | 複数バッファ管理クラス（設計中） |

## その他

| ドキュメント | 内容 |
|-------------|------|
| [GITHUB_PAGES_SETUP.md](GITHUB_PAGES_SETUP.md) | GitHub Pages デプロイ設定 |

## 構想段階

未実装のアイデアは [ideas/](ideas/) フォルダにあります。
