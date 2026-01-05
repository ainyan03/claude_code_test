# Changelog

このプロジェクトの主要な変更を記録します。

## [Unreleased] - 2026-01-05

### 📊 デバッグ機能強化: 詳細パフォーマンス計測

#### 概要
WASM内部の処理時間を工程ごとに計測し、コンソールに詳細ログを出力する機能を追加しました。

#### 計測対象
- **Filter**: フィルタ処理（明るさ、グレースケール、ボックスブラー、アルファ調整）
- **Affine**: アフィン変換処理
- **Composite**: 画像合成処理（mergeImages）
- **Convert**: ピクセルフォーマット変換（RGBA8_Straight ↔ RGBA16_Premultiplied）
- **Output**: 最終出力変換（ViewPort → Image）

#### 実装内容

**C++ (node_graph.h)**:
- `PerfMetrics`構造体を追加（各工程の累積時間と実行回数を保持）
- `getPerfMetrics()`メソッドで計測結果を取得可能

**C++ (node_graph.cpp)**:
- 各処理の前後で`std::chrono::high_resolution_clock`による計測を実装

**C++ (bindings.cpp)**:
- `getPerfMetrics()`をJavaScript側に公開

**JavaScript (app.js)**:
- 詳細ログ出力を実装

#### コンソール出力例
```
[Perf] Total: 45.2ms | WASM: 38.5ms (Affine: 12.34ms (x1), Composite: 8.56ms (x1), Convert: 5.23ms (x2), Output: 10.12ms) | Draw: 6.5ms
```

---

### 🎨 UI/UX改善: サイドバーメニューとレイアウト最適化

#### 概要
ブラウザUIの使い勝手を大幅に改善しました。画像ライブラリと出力設定を左サイドバーに移動し、ビューポート固定レイアウトを導入しました。

#### PR #21: 左サイドバーメニューとUIレイアウト改善

**左サイドバーメニュー**:
- 画像ライブラリを左サイドバーに移動
- 出力設定（キャンバスサイズ、原点、表示倍率）をサイドバー下部に配置
- トグルボタンで開閉可能（ESCキー、オーバーレイクリックでも閉じる）
- レスポンシブデザイン対応
  - PC: サイドバー表示時もメインコンテンツ操作可能
  - モバイル: オーバーレイ表示でサイドバーにフォーカス

**リサイズ可能なスプリッター**:
- ノードグラフとプレビューエリア間にドラッグ可能なスプリッターを追加
- マウス・タッチ両対応
- 各セクションは独立してスクロール

**プレビューエリアの改善**:
- スクロールバー常時表示（WebKitスタイリング）
- スクロール位置の自動中央調整（初期化時、リサイズ時、倍率変更時）
- 表示倍率スライダー追加（1x〜5x、0.5刻み）

**ヘッダーのコンパクト化**:
- 縦並び → 横並びレイアウトに変更
- パディング・フォントサイズを縮小
- コンテンツ表示領域を拡大

**コミット**:
- `ef81bd3` - feat: Add left sidebar menu for image library and output settings
- `98bd44d` - fix: Improve sidebar output settings layout
- `f270be9` - feat: Add resizable splitter between node graph and preview
- `60b9f8b` - feat: Improve preview area with scrollbar, auto-centering, and scale control

---

### 🧹 合成ノードの簡素化: アフィンパラメータ削除

#### 概要
合成ノードからアフィン変換機能を削除し、役割を「入力画像の合成」のみに限定しました。アフィン変換が必要な場合は、専用の「アフィン変換ノード」を使用します。

#### 削除内容

**JavaScript (app.js)**:
- 詳細ボタン（「⚙️ 詳細」）
- 合成ノードのaffineParamsオブジェクト
- 合成ノード編集パネル関連関数（initializeCompositeEditPanel等）

**CSS (style.css)**:
- .composite-edit-panel関連スタイル（約47行）

**C++ (bindings.cpp)**:
- 合成ノードのaffineParams読み取り処理

**C++ (node_graph.h)**:
- compositeTransformフィールド

**C++ (node_graph.cpp)**:
- 合成ノードのアフィン変換処理（約53行）

#### 設計方針
- **合成ノード**: 入力画像のアルファブレンディングのみ
- **アフィン変換ノード**: 平行移動、回転、スケールを担当
- 機能の明確な分離により、ノードグラフの設計がシンプルに

---

## [Unreleased] - 2026-01-04

### 🧹 Phase 6: WebAssemblyバインディングの整理と未使用コード削除

#### 概要
Phase 5で完了したViewPort移行に続き、JavaScript側をNodeGraphEvaluator専用に移行し、不要となったImageProcessorWrapper関連コードを完全に削除しました。

#### PR #14: JS側のImageProcessor依存削除
**変更ファイル**: `web/app.js`

**削除内容**:
- `let processor;` グローバル変数の削除
- `ImageProcessor` の初期化ブロック削除
- `processor.setCanvasSize()` 呼び出しの削除（元々バインディングに存在しないバグだった）
- WebAssemblyタイムアウトチェックを `graphEvaluator` に変更

**結果**: app.jsは`NodeGraphEvaluator`のみを使用するように統一

#### PR #15: C++側の未使用コード削除
**変更ファイル**: `src/bindings.cpp`

**削除内容**:
- `ImageProcessorWrapper` クラス全体（9メソッド）
  - `applyFilterToImage()`
  - `applyTransformToImage()`
  - `mergeImages()`
  - `toPremultiplied()`
  - `fromPremultiplied()`
  - `applyFilterToImage16()`
  - `applyTransformToImage16()`
  - `mergeImages16()`
  - `createAffineMatrix()`
- EMSCRIPTEN_BINDINGS の `ImageProcessor` エントリ
- `viewPortFromJSImage()` / `viewPortToJSImage()` ヘルパー関数
- 未使用の `#include` ディレクティブ
  - `image_processor.h`
  - `viewport.h`
  - `pixel_format.h`

**コード削減**: 約410行

#### 現在のアーキテクチャ

**JavaScript側**:
- `NodeGraphEvaluator` のみを使用
- `ImageProcessor` への依存なし

**C++バインディング**:
```cpp
EMSCRIPTEN_BINDINGS(image_transform) {
    class_<NodeGraphEvaluatorWrapper>("NodeGraphEvaluator")
        .constructor<int, int>()
        .function("setCanvasSize", ...)
        .function("registerImage", ...)
        .function("setNodes", ...)
        .function("setConnections", ...)
        .function("evaluateGraph", ...)
        .function("getPerfMetrics", ...);
}
```

**ファイル構造**:
```
src/bindings.cpp: NodeGraphEvaluatorWrapperのみを公開（~200行、以前は~610行）
```

#### 利点
- ✅ **コードベースの簡素化**: 410行の未使用コード削除
- ✅ **バグ修正**: 存在しない`processor.setCanvasSize()`呼び出しを削除
- ✅ **保守性向上**: 使用されていないコードパスの排除
- ✅ **ビルドサイズ削減**: 不要なバインディングの削除

#### コミット
- `a1906ba` - Migrate app.js to use only NodeGraphEvaluator
- `a328b5f` - Remove unused ImageProcessorWrapper from bindings

---

## [Unreleased] - 2026-01-04

### 🐛 奇数幅画像の斜め歪みバグ修正

#### 問題
439px幅の画像で45度の斜め線状の歪みが発生していた。

#### 原因
ViewPortは16バイトアライメントのstrideを持つが、`fromImage()`と`toImage()`がデータをフラット配列として扱っていた。

#### 修正
`image_processor.cpp`の`fromImage()`と`toImage()`を行ごとのアクセスに修正し、`getPixelPtr()`を使用してstrideを正しく処理するようにした。

#### コミット
- `c8e0e32` - Fix diagonal distortion bug for odd-width images

---

## [Unreleased] - 2026-01-03

### 🎯 Phase 5B-D: ViewPort完全移行とImage16削除

#### 概要
Phase 5Aで実装したViewPort統一画像型に、すべての内部処理を移行しました。Image16型を完全に削除し、ViewPortが内部処理の統一画像型として機能するようになりました。

#### Phase 5B: ViewPortベース処理関数の実装

**変更ファイル**:
- **src/filters.h**: `ImageFilter16` → `ImageFilter` にリネーム
  - すべてのフィルタクラスがViewPortベースに（`BrightnessFilter`, `GrayscaleFilter`, `BoxBlurFilter`）
  - `apply(const ViewPort& input)` メソッドで統一
  - `getPreferredInputFormat()` はViewPort形式に対応

- **src/filters.cpp**: すべてのフィルタ実装をViewPortに移行
  - `getPixelPtr<uint16_t>()` テンプレートメソッドで型安全なアクセス
  - PixelFormatRegistryを使用した形式変換
  - 例: `BrightnessFilter::apply(const ViewPort& input)`

- **src/image_processor.h**: メソッド名を明確化
  - `toPremultiplied()` → `fromImage()` (8bit → ViewPort変換)
  - `fromPremultiplied()` → `toImage()` (ViewPort → 8bit変換)
  - `applyFilterToImage16()` → `applyFilter()` (ViewPortベース)
  - `applyTransformToImage16()` → `applyTransform()` (ViewPortベース)
  - `mergeImages16()` → `mergeImages()` (ViewPortベース)

- **src/image_processor.cpp**: すべての処理関数をViewPort実装に
  - `fromImage()`: 8bit RGBA → ViewPort (16bit Premultiplied) 変換
  - `toImage()`: ViewPort → 8bit RGBA 変換
  - `applyFilter()`: ViewPortベースフィルタ処理
  - `applyTransform()`: ViewPortベースアフィン変換
  - `mergeImages()`: ViewPortベース画像合成
  - `convertPixelFormat()`: ViewPort形式間変換

**コミット**: `c9453c2` - Fix: Include image_types.h in node_graph.h for complete type definitions

#### Phase 5C: 既存コードの段階的移行

**変更ファイル**:
- **src/node_graph.h**:
  - `#include "image_types.h"` を追加（AffineParams/AffineMatrixの完全な型定義が必要）
  - 内部キャッシュをViewPortに変更
    - `std::map<int, Image16> layerPremulCache` → `std::map<int, ViewPort> layerPremulCache`
    - `std::map<std::string, Image16> nodeResultCache` → `std::map<std::string, ViewPort> nodeResultCache`
  - `evaluateNode()` の戻り値を `Image16` → `ViewPort` に変更
  - `getLayerPremultiplied()` の戻り値を `Image16` → `ViewPort` に変更

- **src/node_graph.cpp**: すべてのノード評価をViewPortベースに
  - `evaluateNode()`: ViewPortを返すように変更
  - 画像ノード: `processor.fromImage()` でViewPort変換
  - フィルタノード: `processor.applyFilter()` でViewPort処理
  - 合成ノード: `processor.mergeImages()` でViewPort合成
  - アフィン変換ノード: `processor.applyTransform()` でViewPort変換
  - `evaluateGraph()`: 最後に `processor.toImage()` で8bit変換

- **src/bindings.cpp**: 内部処理をViewPortに統一、JavaScript APIは互換性維持
  - `toPremultiplied()`: `processor.fromImage()` を使用
  - `applyFilterToImage()`: `processor.applyFilter()` を使用
  - `applyTransformToImage()`: `processor.applyTransform()` を使用
  - `mergeImages()`: `processor.mergeImages()` を使用

**ビルドエラー修正**:
- 問題: `field has incomplete type 'AffineParams'` エラー
- 原因: node_graph.hでAffineParams/AffineMatrixが前方宣言のみで完全定義がなかった
- 解決: `#include "image_types.h"` を追加して完全な型定義を取得

#### Phase 5D: Image16の完全削除

**変更ファイル**:
- **src/image_types.h**:
  - `Image16` 構造体を完全削除（約20行削除）
  - `Image` 構造体は保持（WebAssembly API境界で使用）
  - AffineParams と AffineMatrix は保持（引き続き使用中）

- **src/viewport.h**:
  - `Image16` 前方宣言を削除
  - `fromImage16()` 静的メソッドを削除
  - `toImage16()` メソッドを削除
  - コメントを更新（「Image/Image16を統合」→「統一画像型として設計」）

- **src/viewport.cpp**:
  - `fromImage16()` 実装を削除
  - `toImage16()` 実装を削除
  - コメントセクションを「Image からの変換」「Image への変換」に更新

**確認**:
- `grep -r "Image16"` で残存参照がないことを確認 → ✅ なし

**コミット**: `ee41eb5` - Phase 5D: Remove Image16 type and migration helpers

#### 全体的な成果

**削除されたコード**:
- Image16構造体: ~20行
- fromImage16/toImage16実装: ~50行
- 合計: ~70行の削減

**変更されたコード**:
- filters.h/cpp: ViewPortベースに完全移行
- image_processor.h/cpp: ViewPortベースに完全移行
- node_graph.h/cpp: ViewPortベースに完全移行
- bindings.cpp: 内部処理をViewPortに統一

**現在の型使用状況**:
- **Image**: 8bit RGBA、WebAssembly API境界のみで使用
- **ViewPort**: 内部処理の統一画像型、すべての処理関数で使用
- **Image16**: ✅ 完全削除

**利点**:
- ✅ コード重複の完全排除
- ✅ 型安全性の向上（PixelFormatIDによる実行時検証）
- ✅ メモリ効率の改善（16バイトアライメント、カスタムアロケータ）
- ✅ ROI/viewport機能の有効化（ゼロコピーサブ領域処理）
- ✅ 組込み環境対応（FixedBufferAllocator使用可能）

---

## [Unreleased] - 2026-01-03

### 🖼️ Phase 5A: ViewPort統一画像型の導入（組込み環境対応）

#### 背景と目的
Image（8bit）とImage16（16bit）の2つの型が存在し、コード重複とメンテナンスコストが発生していました。また、組込み環境への移植を前提として、以下の機能が必要でした：
- カスタムアロケータによるメモリ管理
- ビューポート/ROI機能（メモリコピーなしでサブ領域を参照）
- 任意のピクセルフォーマット対応（Phase 1の拡張可能システムと統合）

#### 新規ファイル
**src/viewport.h** (~200行):
- ViewPort構造体の完全な定義
- 包括的なドキュメントコメント
- RAII原則に従った設計
- テンプレートメソッド（型安全なピクセルアクセス）

**src/viewport.cpp** (~350行):
- 完全なRAII実装
  - コンストラクタ/デストラクタ
  - コピーセマンティクス（ディープコピー）
  - ムーブセマンティクス（所有権移転）
- メモリ管理
  - 16バイトアライメント対応
  - ストライド計算（行パディング）
  - カスタムアロケータ統合
- ビューポート機能
  - `createSubView()`: ゼロコピーサブ領域作成
  - 親子関係管理
- 変換ヘルパー
  - `fromImage()` / `toImage()`
  - `fromImage16()` / `toImage16()`

#### 修正ファイル
**src/image_types.h**:
- ImageとImage16に非推奨コメント追加
- 段階的移行戦略を明示
```cpp
// 【非推奨】このImageは将来的にViewPortに統合されます。
// 新規コードではViewPortの使用を推奨します。
// 移行完了後にこの型は削除されます。
```

**build.sh**:
- viewport.cppをコンパイル対象に追加

#### ViewPortの主要機能

**1. 統一された画像型**
```cpp
// 任意のフォーマットに対応
ViewPort img8(800, 600, PixelFormatIDs::RGBA8_Straight);
ViewPort img16(800, 600, PixelFormatIDs::RGBA16_Premultiplied);
ViewPort rgb565(800, 600, PixelFormatIDs::RGB565_LE);
```

**2. ビューポート機能（ゼロコピー）**
```cpp
ViewPort fullImage(1920, 1080, PixelFormatIDs::RGBA16_Premultiplied);
ViewPort roi = fullImage.createSubView(100, 100, 640, 480);
// roiは親画像のメモリを直接参照（コピーなし）
processFilter(roi);  // サブ領域のみ処理
```

**3. カスタムアロケータ対応**
```cpp
// 組込み環境での静的バッファ使用
uint8_t staticBuffer[1024 * 1024];
FixedBufferAllocator fixedAlloc(staticBuffer, sizeof(staticBuffer));
ViewPort img(800, 600, PixelFormatIDs::RGBA16_Premultiplied, &fixedAlloc);
```

**4. 後方互換性**
```cpp
// 既存コードからの移行
Image oldImg(800, 600);
ViewPort newImg = ViewPort::fromImage(oldImg);

// 既存コードへの変換
Image backToOld = newImg.toImage();
```

#### 設計の利点
- ✅ **コード重複の排除**: Image/Image16の統一
- ✅ **メモリ効率**: std::vectorのオーバーヘッド排除、16バイトアライメント
- ✅ **ROI処理**: メモリコピーなしで部分領域処理が可能
- ✅ **組込み環境対応**: カスタムアロケータで柔軟なメモリ管理
- ✅ **拡張性**: 任意のピクセルフォーマットに対応（Phase 1と完全統合）
- ✅ **型安全性**: PixelFormatIDによる実行時検証

#### 移行戦略（段階的アプローチ）
1. **Phase 5A（完了）**: ViewPort基盤実装
2. **Phase 5B（予定）**: ViewPort版処理関数の実装
3. **Phase 5C（予定）**: 既存コードの段階的移行
4. **Phase 5D（予定）**: Image/Image16の完全削除

#### コミット
- `c41fe91` - Phase 5A: Introduce ViewPort unified image type with viewport support

#### コード統計
```
新規追加: 555行
- viewport.h: ~200行（ドキュメント含む）
- viewport.cpp: ~350行（完全実装）
- deprecation comments: ~5行
変更: 4ファイル
```

---

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
