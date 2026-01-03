# ViewPort統一画像型への完全移行（Phase 5A-D）

## 概要
Image（8bit）とImage16（16bit）の2つの画像型を、ViewPort統一画像型に完全移行しました。Image16を削除し、すべての内部処理をViewPortベースに統一しました。

## 主な変更内容

### Phase 5B: ViewPortベース処理関数の実装
- **filters.h/cpp**: `ImageFilter16` → `ImageFilter` にリネーム、ViewPortベースに移行
- **image_processor.h/cpp**: すべてのメソッドをViewPortベースに更新
  - `toPremultiplied()` → `fromImage()` (8bit → ViewPort変換)
  - `fromPremultiplied()` → `toImage()` (ViewPort → 8bit変換)
  - `applyFilterToImage16()` → `applyFilter()`
  - `applyTransformToImage16()` → `applyTransform()`
  - `mergeImages16()` → `mergeImages()`

### Phase 5C: 既存コードの段階的移行
- **node_graph.h/cpp**: 内部キャッシュをViewPortに変更、すべてのノード評価をViewPortベースに
- **bindings.cpp**: 内部処理をViewPortに統一（JavaScript APIは互換性維持）
- ビルドエラー修正: `#include "image_types.h"` を追加

### Phase 5D: Image16の完全削除
- **image_types.h**: `Image16` 構造体を削除（~20行削減）
- **viewport.h/cpp**: `fromImage16()`/`toImage16()` メソッドを削除（~50行削減）
- コメント更新: 「Image/Image16を統合」→「統一画像型として設計」

## 技術的成果

### コード削減
- Image16関連: 約70行削除
- コード重複の完全排除

### 現在の型使用状況
- **Image**: 8bit RGBA、WebAssembly API境界のみで使用
- **ViewPort**: 内部処理の統一画像型（すべてのフィルタ、画像処理、ノードグラフ評価）
- **Image16**: ✅ 完全削除

### 利点
- ✅ コード重複の完全排除
- ✅ 型安全性の向上（PixelFormatIDによる実行時検証）
- ✅ メモリ効率の改善（16バイトアライメント、カスタムアロケータ）
- ✅ ROI/viewport機能の有効化（ゼロコピーサブ領域処理）
- ✅ 組込み環境対応（FixedBufferAllocator使用可能）

## コミット履歴
- `120aa57` - Docs: Document Phase 5B-D (ViewPort migration) completion
- `ee41eb5` - Phase 5D: Remove Image16 type and migration helpers
- `c9453c2` - Fix: Include image_types.h in node_graph.h for complete type definitions
- `9b193b5` - Phase 5B-C: Migrate processing code to ViewPort unified image type

## テスト
- ✅ ビルド成功（GitHub Actions）
- ✅ 動作確認済み（ユーザー確認）

## 関連ドキュメント
- PROJECT_STATUS.md: Phase 5A-D完了を反映
- CHANGELOG.md: 詳細な変更履歴を追加
