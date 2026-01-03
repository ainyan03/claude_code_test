# Changelog

このプロジェクトの主要な変更を記録します。

## [Unreleased] - 2026-01-03

### 🎨 拡張可能ピクセルフォーマットシステムの実装（4フェーズ）

#### 背景と問題
レビュアーから「ブライトネスやグレースケールフィルタをプリマルチプライドアルファデータに直接適用するのは数学的に不適切」という指摘を受けました。この問題を解決するため、拡張可能なピクセルフォーマットシステムを段階的に実装しました。

#### フェーズ1: 基盤システムの実装
**新規ファイル**:
- `src/pixel_format.h`: ピクセルフォーマット記述子の定義
  - `PixelFormatID`: フォーマット識別子（uint32_t）
  - `PixelFormatDescriptor`: ビット配置、エンディアン、チャンネル情報を記述
  - ビットパック形式対応（1,2,3,4 bits/pixel）
  - インデックスカラー/パレット対応
  - 可変ピクセル/ユニット比率（例: 3bpp = 8ピクセル/3バイト）
- `src/pixel_format_registry.h/cpp`: フォーマット管理とレジストリパターン
  - シングルトンレジストリでフォーマットを一元管理
  - 組み込み形式: `RGBA16_Straight`, `RGBA16_Premultiplied`
  - 形式間変換関数の提供
  - ユーザー定義形式の登録サポート
- `src/image_allocator.h`: 組み込み環境対応のメモリ管理
  - `ImageAllocator` インターフェース
  - `DefaultAllocator` (malloc/free)
  - `FixedBufferAllocator` (静的バッファ)

**設計の特徴**:
- 将来のRGB565、RGB332、インデックスカラー等に対応可能
- Lazy conversion パターン（必要な時のみ変換）
- 後方互換性を維持（既存コードは変更不要）

**コミット**: `c04781f` - Phase 1: Add extensible pixel format system foundation

#### フェーズ2: Image16構造体の拡張
**変更ファイル**:
- `src/image_types.h`: Image16構造体の拡張
  - `formatID` フィールドを追加（新形式システム）
  - レガシー `PixelFormat` 列挙型を維持（後方互換性）
  - オーバーロードされたコンストラクタ
    - `Image16(int w, int h, PixelFormat fmt)` - 旧API
    - `Image16(int w, int h, PixelFormatID fmtID)` - 新API
  - デフォルトは `RGBA16_Premultiplied` 形式

**後方互換性**:
- 既存コードは一切変更せずにコンパイル可能
- 新旧両方のAPIが共存

**コミット**: `e1a05eb` - Phase 2: Extend Image16 with new format system (backward compatible)

#### フェーズ3: フィルタの修正（コア問題の解決）
**変更ファイル**:
- `src/filters.h`: `ImageFilter16` 基底クラスの拡張
  - `getPreferredInputFormat()`: フィルタが要求する入力形式を宣言
  - `getOutputFormat()`: フィルタの出力形式を宣言
- `src/filters.cpp`: **重要な修正** - レビュー指摘への対応
  - `BrightnessFilter16`: Straight形式で処理（数学的に正確）
    - RGB値に直接加算（プリマルチプライド形式では誤り）
    - オーバーフロー/アンダーフローを適切に処理
  - `GrayscaleFilter16`: Straight形式で処理
    - RGB値の平均を計算（プリマルチプライド形式では暗くなる）
  - 自動形式変換: 入力が異なる形式の場合、フィルタ内で変換
- `src/image_processor.h/cpp`: `convertPixelFormat()` メソッド追加
  - レジストリを使用した形式間変換
  - 同一形式の場合は変換をスキップ（最適化）

**数学的正確性の例**:
```
問題のケース（旧実装）:
- ピクセル: RGB(200, 200, 200), A(128/255)
- プリマルチプライド: RGB(100, 100, 100), A(128)
- ブライトネス+50を適用 → RGB(150, 150, 150) ✗ 誤り
- 正しい結果: RGB(250, 250, 250) ✓

修正後（新実装）:
- Straight形式に変換: RGB(200, 200, 200), A(128)
- ブライトネス+50を適用: RGB(250, 250, 250), A(128)
- 必要に応じてPremultipliedに変換 ✓ 正確
```

**コミット**: `614cbdf` - Phase 3: Fix premultiplied alpha filter processing (CRITICAL FIX)

#### フェーズ4: ノードグラフ評価の最適化
**変更ファイル**:
- `src/node_graph.cpp`: ノード境界での自動形式変換
  - **合成ノード**: Premultiplied形式が必須（ブレンディングの数学）
    - 新形式動的入力（3箇所）
    - 旧形式互換入力（input1, input2）
  - **アフィン変換ノード**: Premultiplied形式が適切（エッジブレンディング）
  - 各ノードで入力形式をチェックし、必要に応じて自動変換

**データフロー（最適化）**:
```
imageノード → [Premultiplied]
                   ↓
filterノード → [Straight処理] → [Straight出力]
                   ↓
合成ノード → [自動変換] → [Premultiplied処理]
```

**最適化ポイント**:
- フィルタチェーン内での冗長な変換を回避
  - 例: Brightness → Grayscale は両方Straightで処理、変換は1回のみ
- 必要な箇所でのみ変換（Lazy conversion パターン）
- ノードグラフ評価の透明性（開発者は形式を意識不要）

**コミット**: `85d9464` - Phase 4: Add automatic pixel format conversion in node graph evaluation

#### 全体的な成果
- ✅ レビュアー指摘の問題を完全解決
- ✅ 数学的に正確なフィルタ処理
- ✅ 将来の形式拡張に対応可能（RGB565, インデックスカラー等）
- ✅ 後方互換性を完全維持
- ✅ パフォーマンス最適化（不要な変換を回避）
- ✅ 組み込み環境対応（カスタムアロケータ）

#### ビルド
- `build.sh`: `pixel_format_registry.cpp` をコンパイル対象に追加

---

## [Unreleased] - 2026-01-03

### 🏗️ コードリファクタリング: モジュラーファイル構造への分離

#### アーキテクチャ改善
- **ファイル分離**: モノリシックな構造から明確な責務を持つモジュールへ
  - `image_types.h`: 基本型定義（Image, Image16, AffineParams等）
  - `filters.h/cpp`: フィルタクラス群（ImageFilter16派生クラス）
  - `image_processor.h/cpp`: コア画像処理エンジン（16bit処理関数）
  - `node_graph.h/cpp`: ノードグラフ評価エンジン
  - `bindings.cpp`: JavaScriptバインディング

#### コード整理
- **不要コード削除**: 約350行の旧レイヤーシステムコードを削除
  - 削除された構造体: `FilterNodeInfo`, `Layer`（一部フィールド）
  - 削除されたメソッド: 15個の非推奨メソッド
- **シンプル化**: ImageProcessorをコア機能のみに絞り込み

#### メリット
- **コンパイル時間短縮**: 部分的な変更が全体の再ビルドを引き起こさない
- **保守性向上**: コードの所在が直感的で見つけやすい
- **再利用性向上**: フィルタや処理エンジンを独立して利用可能
- **依存関係の明確化**: 循環依存なし、クリーンなモジュール設計

#### コミット
- `e1e64e5`: Cleanup: Remove deprecated layer system code
- `85c53ec`: Refactor: Separate code into modular files

### 🚀 パフォーマンス最適化: 16bit専用アーキテクチャへの移行

#### アーキテクチャ変更
- **16bit専用処理パイプライン**: すべてのフィルタ処理を16bit premultiplied alphaで統一
  - 旧: 8bit → 16bit → フィルタ処理 → 16bit → 8bit (複数回の変換オーバーヘッド)
  - 新: 16bit → フィルタ処理 → 16bit (変換は入出力時のみ)
- **クラスベースフィルタアーキテクチャ**: 拡張性を考慮した設計
  - `ImageFilter16` 基底クラスの導入
  - パラメータ構造体による型安全な設定
  - シンプルファクトリパターンによるフィルタ生成

#### 実装の改善
- **8bitフィルタコード削除**: 約150行の冗長なコードを削除
- **メモリ最適化**: `apply()` メソッドでコピーコンストラクタを回避
- **API簡素化**: `applyFilterToImage16()` を93行から26行に短縮
- **最適化された `getName()`**: `std::string` から `const char*` に変更してアロケーション削減

#### 対応フィルタ
- `BrightnessFilter16`: 明るさ調整（16bit精度）
- `GrayscaleFilter16`: グレースケール変換
- `BoxBlurFilter16`: ボックスブラー（2パス処理）

#### バインディング更新
- JavaScript APIは互換性維持（内部的に16bit処理を使用）
- `applyFilterToImage`, `applyTransformToImage`, `mergeImages` が16bitパイプライン経由に

#### コミット
- `789419f`: Refactor filter architecture: introduce class-based 16bit filters
- `ef686a6`: Remove 8bit filter code: complete migration to 16bit-only architecture
- `923fb67`: Fix build errors: remove remaining 8bit filter references
- `706bd42`: Update bindings to use 16bit processing pipeline

## [Unreleased] - 2026-01-02

### 🎉 大規模アーキテクチャ変更: レイヤーシステム → ノードグラフエディタ

#### 追加機能
- **ノードグラフエディタ**: ビジュアルプログラミング形式の画像処理パイプライン構築
  - 画像ノード: ライブラリから画像を選択
  - アフィン変換ノード: 平行移動、回転、スケール調整
  - 合成ノード: 複数画像のアルファブレンディング
  - フィルタノード: 明るさ・コントラスト調整
  - 出力ノード: 最終結果のプレビュー
- **画像ライブラリ**: 複数画像の一元管理とノードへの割り当て
- **ノード操作**:
  - ドラッグ&ドロップでノード配置
  - ワイヤーによるノード接続（ビジュアルデータフロー）
  - コンテキストメニュー（右クリック）でノード追加・削除
  - パラメータ/行列表示モード切り替え
- **C++ NodeGraphEvaluator**: トポロジカルソートによるグラフ評価エンジン
  - 依存関係の自動解決
  - メモ化による重複計算の回避
  - ノードタイプ別の処理分岐

#### 削除機能
- 旧レイヤーベースシステムを完全削除（461行のコード削除）
  - レイヤー管理UI（上下ボタン、チェックボックス）
  - ImageProcessor クラスのレイヤー管理機能
  - JavaScript側のレイヤー配列管理

#### UI/UX改善（12コミット）
- ノードの動的高さ調整（コンテンツに応じて自動リサイズ）
- ポート位置のラグ修正（CSS transitionを削除しリアルタイム更新）
- ポートヒットエリアの透明化（クリックしやすさ向上）
- キャンバスリサイズ時の接続線自動更新
- スライダーラベルの配置改善
- ノードドラッグ中の接続線リアルタイム更新

#### ドキュメント更新
- **README.md**: ノードグラフアーキテクチャに完全書き換え
  - レイヤー操作の記述を削除
  - ノードベースの使い方を追加
  - NodeGraphEvaluator の技術詳細を追加
- **QUICKSTART.md**: ノード操作の手順に更新
  - GitHub Pages を主要なアクセス方法として追加
  - JavaScript fallback への言及を削除
- **GITHUB_PAGES_SETUP.md**: WebAssembly専用に更新
  - JavaScript backend の記述を削除

### 技術的詳細

#### アーキテクチャ
- **移行前**: レイヤーベースシステム
  ```
  [画像1] → [パラメータ調整] → \
  [画像2] → [パラメータ調整] → → [合成処理] → [出力]
  [画像3] → [パラメータ調整] → /
  ```

- **移行後**: ノードグラフシステム
  ```
  [画像ライブラリ]
       ↓
  [画像ノード] → [アフィン変換] → \
  [画像ノード] → [フィルタ] -----→ [合成] → [出力]
  ```

#### 実装の変更点
- C++側: `ImageProcessor::processLayers()` → `NodeGraphEvaluator::evaluate()`
- JavaScript側: グローバル配列 `layers[]` → `globalNodes[]` + `globalConnections[]`
- データフロー: 固定パイプライン → 動的グラフ評価

### コミット履歴（時系列）
```
a766b71 Fix rotation and optimize blur filter
9b941bf Fix layer transform parameters not being applied
e79a6bd Phase 1: Add image library management (WIP)
29b32db Implement image node alpha support in C++
b3808b6 Implement affine transform node with dual modes
806d30e Implement node deletion with context menu
f71d3f5 Remove legacy layer system code
c581b65 Fix: Remove all legacy layer system code completely
a3f37e3 Implement four UI/UX improvements
880dc8f Fix four node graph UI/UX issues
7456152 Implement five additional UI/UX improvements
5e148ff Fix port lag during node drag - update hit areas
6b5c5bc Fix port position lag - remove CSS transition from cx/cy
7c1ce3d Fix two issues: canvas resize and slider alignment
375b35a Update documentation to reflect node-graph architecture
```

### 次のステップ（推奨）
1. このブランチを main にマージ
2. GitHub Actions デプロイ設定を main ブランチに変更
3. 必要に応じて追加のノードタイプを実装（例: ぼかしノード、トリミングノード等）
