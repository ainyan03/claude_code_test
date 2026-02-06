# Changelog

All notable changes to fleximg will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

> **Note**: 詳細な変更履歴は [docs/CHANGELOG_v2_detailed.md](docs/CHANGELOG_v2_detailed.md) を参照してください。

---

## [2.63.x] - 2026-02-05～2026-02-06

### ハイライト

v2.63系では、パフォーマンス最適化とAPI整理を中心に28回のリリースを実施しました。

#### 主な新機能

- **Alpha8/Grayscale8 単一チャンネル バイリニア補間** (v2.63.28)
  - 1チャンネルフォーマットの補間処理を最適化（メモリ4倍削減、演算量約1/4削減）

- **カラーキー透過機能** (v2.63.27)
  - アルファチャンネルを持たないフォーマットで特定色を透明として扱える機能
  - `SourceNode::setColorKey()` / `clearColorKey()` API追加

- **EdgeFadeFlags** (v2.63.15)
  - バイリニア補間の辺ごとのフェードアウト制御
  - NinePatch内部エッジでのフェードアウト無効化に使用

#### 主な改善・最適化

- **ImageBuffer の簡素化** (v2.63.26)
  - validSegments 複数セグメント追跡を廃止（265行削減）
  - ゼロ初期化 + 常にブレンド方式に変更し、パフォーマンス向上

- **CompositeNode 合成処理の最適化** (v2.63.25)
  - 事前集計方式に変更、ベンチマークで約27%の性能改善（N=32時）

- **SourceNode::getDataRange() の正確化** (v2.63.24)
  - DDAベースの厳密計算でNinePatchの余白バッファを削減

- **ImageBuffer 座標系の統一** (v2.63.22)
  - `startX_` (int16_t) → `origin_` (Point, Q16.16固定小数点) に拡張
  - M5Stackでの性能退行を解消

- **バイリニア補間のマルチフォーマット対応** (v2.63.16)
  - Index8を含む全フォーマットでバイリニア補間が利用可能に

#### API変更

- **ImageBufferSet**: オフセットパラメータ削除、origin統一 (v2.63.23)
- **Node::getDataRangeBounds()**: AABB由来の最大範囲上限を返すメソッド追加 (v2.63.24)
- **ImageBuffer::blendFrom()**: under合成メソッドの簡素化 (v2.63.25)

#### バグ修正

- RenderResponseプール管理の再設計（DOUBLE RELEASE修正） (v2.63.6)
- NinePatch角パッチの描画位置修正 (v2.63.21)
- SourceNode AABB拡張（バイリニア補間時のフェードアウト領域反映） (v2.63.20)

詳細は [docs/CHANGELOG_v2_detailed.md の v2.63.x セクション](docs/CHANGELOG_v2_detailed.md#2630---2026-02-05) を参照してください。

---

## [2.62.x] - 2026-02-01

### ハイライト

v2.62系では、パイプラインリソース管理の一元化とレスポンス参照パターンへの移行を実施しました。

#### 主な変更

- **RenderContext導入** (v2.62.0)
  - パイプライン動的リソース管理の一元化
  - PrepareRequest.allocator/entryPool を context に統合
  - 将来拡張に対応（PerfMetrics, TextureCache等）

- **RenderResponse参照ベースパターン** (v2.62.3)
  - 値渡しから参照渡しへ移行、ムーブコスト削減
  - RenderContextがRenderResponseプール（MAX_RESPONSES=64）を管理

- **ImageBufferSet::transferFrom()**: バッチ転送API追加 (v2.62.2)
  - 上流結果の統合を効率化

#### バグ修正

- ImageBufferEntryPool::release(): エントリ返却時にバッファをリセット (v2.62.4)

詳細は [docs/CHANGELOG_v2_detailed.md の v2.62.x セクション](docs/CHANGELOG_v2_detailed.md#2620---2026-02-01) を参照してください。

---

## [2.61.0 以前] - 2026-01-09～2026-01-31

v2.0.0 から v2.61.0 までの詳細な変更履歴は [docs/CHANGELOG_v2_detailed.md](docs/CHANGELOG_v2_detailed.md) を参照してください。

### 主なマイルストーン

- **v2.56.0**: インデックスカラー対応（Index8, Grayscale8, Alpha8）
- **v2.50.0**: NinePatchSourceNode追加
- **v2.40.0**: ガウシアンブラー実装
- **v2.30.0**: メモリプール最適化
- **v2.20.0**: レンダリングパイプライン刷新
- **v2.0.0**: メジャーバージョンリリース
