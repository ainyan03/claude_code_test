# Changelog v2.0～v2.61 詳細版

このファイルは fleximg v2.0.0 から v2.61.0 までの詳細な変更履歴を保持しています。

現在の変更履歴は [CHANGELOG.md](../CHANGELOG.md) を参照してください。

---

## [2.61.0] - 2026-01-31

### リファクタリング

- **CompositeNode onPullProcess 最適化**:
  - 3分割処理を廃止し、描画済み範囲配列（drawnRanges_）方式に変更
  - 描画済み領域をソート済み配列で管理し、重複/非重複を正確に判定
  - 離散的な描画領域を正しく追跡可能に（N層合成対応）
  - ギャップ領域のみゼロクリア、左右端はcropViewで除去（効率化）

---

## [2.60.0] - 2026-01-31

### パフォーマンス改善

- **M5Stackベンチマーク精度向上**:
  - CPUサイクルカウンター（CCOUNTレジスタ）によるナノ秒精度計測
  - 各イテレーションで割り込み禁止による外部要因排除
  - 最小値採用方式で測定ノイズを除去
  - DDA専用ラインバッファ（512px）で高スケールテスト対応
- **copyRowDDA関数群の最適化**:
  - Y座標一定パス（H）: BPP3で2ピクセル単位展開に調整
  - 早期return追加による分岐削減
  - コンパイラのインライン判断に委任（明示的inline削除）

### リファクタリング

- **copyRowDDA_*引数順序統一**: `(dst, src, count, ...)` に統一

---

## [2.59.0] - 2026-01-31

### 新機能

- **HOS（Hyper Operating System）デモ追加**: パトレイバー風ブートスクリーンアニメーション
  - Index8フォーマット + 16色パレットによるメモリ効率化
  - 半透明パレットによるレイヤー合成
  - M5Stack Core2/CoreS3対応

### パフォーマンス改善

- **Index8 expandIndex 最適化**:
  - bpc==4（RGBA8等）: 4ピクセル単位ループ展開
  - bpc==2（RGB565等）: 高速パス追加
  - 境界チェック削除（呼び出し側の責務に変更）
- **RGB565 LE/BE toStraight 最適化**:
  - 2ピクセル単位バッチ処理でメモリ読み込みとLUTアクセスをパイプライン化
  - ベンチマーク: 304us → 205us（**-33%**）
- **RGB565 LE/BE fromStraight 最適化**:
  - 32bitロードで4バイト一括読み込み
  - 2ピクセル単位処理
- **RGB332 fromStraight 最適化**:
  - 2ピクセル単位処理でメモリアクセス効率化

### リファクタリング

- **pixel_format::detail namespace新設**: ピクセルフォーマット内部ヘルパーの分離
- **lut8toN テンプレート化**: lut8to32/lut8to16 を統一テンプレートに
  - 明示的インスタンス化で非inline維持（バイナリサイズ抑制）
  - rgb332_toStraight と index8_expandIndex で共用
- **CompositeNode Index8対応修正**: converter経由でパレット展開してからブレンド

---

## [2.58.0] - 2026-01-29

### パフォーマンス改善

- **RGBA8_Straight blendUnderStraight 最適化**: ESP32等の組み込み環境向けに大幅最適化
  - gotoラベル方式のディスパッチ構造に変更（条件分岐の最適化）
  - `while(--pixelCount >= 0)` ループでESP32のループ命令を活用
  - 連続不透明/透明領域の4ピクセル単位高速スキップ
  - RGB計算を変数に格納後まとめて書き込み（メモリアクセス最適化）
  - ベンチマーク結果（ESP32）:
    - semi（半透明66%）: 1220us → 960us（**-21%**）
    - mixed（混合）: 1056us → 931us（**-12%**）
    - transparent（透明66%）: 428us → 407us（**-5%**）

### リファクタリング

- **blendUnderStraight 実装の簡素化**: 端数ループ削除、カウンタ変数削減
- **旧4pxBatch版を #if 0 化**: バックアップとして保持

---

## [2.57.0] - 2026-01-28

### パフォーマンス改善

- **FormatConverter: 変換パスの事前解決**: `convertFormat` の行単位呼び出しで毎回発生する条件分岐を排除
  - `resolveConverter()` で最適な変換関数を事前解決し、`FormatConverter` 構造体に格納
  - 6種の解決済み変換関数: memcpy / 1段階変換 / Index直接展開 / Index+fromStraight / Index+2段階 / 一般2段階
  - SinkNode: `toFormat` + `view_ops::copy` → `resolveConverter` + 行単位直接書き込み（中間バッファ排除）
  - CompositeNode: 4箇所の `convertFormat` を入力ごとの `resolveConverter` + `converter()` に統一
  - ImageBuffer::toFormat: ループ前に1回解決し全行で再利用

### リファクタリング

- **thread_local 排除**: `convertFormat()` 内の `thread_local std::vector<uint8_t>` を `IAllocator` ベースの一時バッファに置換
  - WASM（シングルスレッド）での無意味なオーバーヘッドを解消
  - 組み込み環境（M5Stack）での互換性向上
  - `<vector>` インクルードが不要になり削除
- **convertFormat 内部実装の簡素化**: 70行の条件分岐を `resolveConverter` + `converter()` の2行に

### テスト

- FormatConverter: resolveConverter の11テスト追加（null/同一/エンディアン/RGBA8直接/一般/Index8/全ペア/bool/大量ピクセル/カスタムアロケータ）

---

## [2.56.0] - 2026-01-28

### 機能追加

- **Grayscale8 ピクセルフォーマット追加**: 8bit グレースケール（輝度チャンネル）
  - BT.601 輝度計算: `(77*R + 150*G + 29*B + 128) >> 8`
  - RGBA8_Straight との双方向変換対応
  - ラウンドトリップ（Gray→RGBA→Gray）で値が保存される
- **Index8 ピクセルフォーマット追加**: 8bit パレットインデックス（256色）
  - `expandIndex` 関数でパレットエントリに展開（パレットフォーマット非依存）
  - RGBA8パレット→RGBA8、RGBA8パレット→RGB565 等の変換に対応
  - 範囲外インデックスはパレット末尾にクランプ
- **ImageBuffer パレットサポート**: インデックスフォーマット用のパレット情報管理
  - `setPalette()` / `palette()` / `paletteFormat()` / `paletteColorCount()` アクセサ
  - コピー/ムーブコンストラクタ・代入でのパレット伝播
  - `toFormat()` でパレット情報を `convertFormat` に自動的に渡す

### リファクタリング

- **ConvertParams → PixelAuxInfo リネーム**: 変換パラメータ構造体にパレット情報を追加
  - `palette`（パレットデータポインタ）、`paletteFormat`（パレットエントリのフォーマット）、`paletteColorCount`（エントリ数）
  - `ConvertParams` / `BlendParams` はエイリアスとして後方互換維持
- **PixelFormatDescriptor 変更**: インデックスカラー対応
  - `toStraightIndexed` / `fromStraightIndexed` を削除
  - `expandIndex` フィールドを追加（インデックス→パレットフォーマット展開）
- **convertFormat シグネチャ変更**: `(srcAux, dstAux)` 方式に統一
  - インデックスフォーマットの自動展開パス追加（1段階/2段階変換）
  - 旧引数（`srcPalette`, `dstPalette`）を廃止

### バグ修正

- **Index8 toStraight 追加**: パレットなし時にグレースケールフォールバック（`idx → R=G=B=idx, A=255`）を提供
  - 修正前: `toStraight` が nullptr で `convertFormat` の conversionBuffer が更新されず、先頭行が全行に繰り返し表示されていた
  - パレット設定時は `expandIndex` パスが先に評価されるため影響なし
- **SinkNode 非アフィンパスのフォーマット変換**: Source→Renderer→Sink 直結時にパレット情報が喪失する問題を修正
  - `ImageBuffer::toFormat` を使用し、アフィンパスと同様にパレット展開を含むフォーマット変換を実施

### WebUI

- PIXEL_FORMATS に Grayscale8（Gray8）と Index8 を追加
- Index8 はソースノードでのみ選択可能（Sinkのフォーマット選択では `sinkDisabled`）
- パレットライブラリ: プリセットパレット（Grayscale256, WebSafe216, 8色）の追加・管理
- ソースノード詳細パネルにパレット選択UI
- パレット状態の保存・復元（Base64シリアライゼーション）

### テスト

- Grayscale8: プロパティ、変換、BT.601輝度計算、ラウンドトリップ（7テスト追加）
- Index8: プロパティ、RGBA8パレット展開、2段階変換、範囲外クランプ（7テスト追加）
- Index8 toStraight: グレースケール展開、ラウンドトリップ、stale buffer 再現（2テスト追加）
- Index8 convertFormat with palette: 直接展開、クランプ、パイプラインシミュレーション（4テスト追加）
- ImageBuffer: パレットアクセサ、コピー/ムーブ伝播、toFormat変換（7テスト追加）

---

## [2.55.0] - 2026-01-27

### パフォーマンス改善

- **CompositeNode 非重複領域の convertFormat 最適化**: 2枚目以降のレイヤーで既存の有効範囲と重複しない領域に `convertFormat` を使用し、不要な `memset` + alpha判定を排除
  - 完全非重複: ギャップのゼロクリア + `convertFormat` で直接書き込み（`blendUnderStraight` をスキップ）
  - 部分重複: 3分割処理（左非重複=`convertFormat`, 重複=`blendUnderStraight`, 右非重複=`convertFormat`）
  - fallback パスの一時バッファサイズを重複幅に縮小（メモリ効率改善）

### テスト

- **CompositeNode 非重複パターンテスト追加**: 5ケース（完全非重複/隣接/部分重複/完全内包/半透明部分重複）

---

## [2.54.0] - 2026-01-27

### 削除

- **RGBA16_Premultiplied フォーマット撤去**: `FLEXIMG_ENABLE_PREMUL` 条件コンパイルコードをすべて削除
  - `PixelFormatDescriptor` 構造体から `isPremultiplied`, `toPremul`, `fromPremul`, `blendUnderPremul` メンバを削除
  - `rgba16_premul.h` ファイルを削除
  - 各フォーマット（RGBA8_Straight, RGB565, RGB888, RGB332, Alpha8）のPremul関連関数を削除
  - `CompositeNode`, `canvas_utils.h` のPremulパスを削除
  - `FormatIdx` から `RGBA16_Premultiplied` を削除、`OpType` を整理（`BlendUnderStraight` → `BlendUnder` にリネーム）
  - テストファイル（`precision_analysis.cpp` 削除、各テストのPremulブロック除去）
  - ビルド設定（`build.sh` の `--premul` フラグ、`platformio.ini` の `FLEXIMG_ENABLE_PREMUL` 行）
  - ベンチマーク（`examples/bench/src/main.cpp` のPremulベンチマーク）
  - ドキュメント更新

---

## [2.53.0] - 2026-01-26

### リファクタリング

- **int_fast16_t 最適化**: ESP32等の32ビットマイコンでのレジスタ演算効率化
  - 対象ノード: SourceNode, CompositeNode, HorizontalBlurNode, VerticalBlurNode, RendererNode, NinePatchSourceNode
  - ループカウンタ・一時変数を `int_fast16_t` に変更
  - 関数引数を `int_fast16_t` に変更（メンバ変数は `int16_t` を維持）
  - `auto + static_cast` パターンを採用（DRY原則）

### ドキュメント

- **CODING_STYLE.md**: レジスタ操作 vs メモリ格納のガイドラインを追加
  - `auto + static_cast` パターンの推奨を明記

---

## [2.52.0] - 2026-01-26

### 機能追加

- **PoolAllocatorAdapter**: `pool_allocator.h` に `IAllocator` インターフェースアダプタを追加
  - `PoolAllocator` を `IAllocator` としてラップし、`RendererNode::setAllocator()` で使用可能に
  - プール確保失敗時は `DefaultAllocator` にフォールバック（`allowFallback` パラメータで制御可能）
  - `FLEXIMG_DEBUG_PERF_METRICS` 定義時のみ統計情報（`Stats`）を保持（リリースビルドでは除外）

### バグ修正

- **RenderResponse の origin 維持**: 範囲外リクエスト時に空レスポンスを返す際、`origin` が失われる問題を修正
  - 影響ノード: `CompositeNode`, `HorizontalBlurNode`, `VerticalBlurNode`, `NinePatchSourceNode`
  - 修正内容: `RenderResponse()` → `RenderResponse(ImageBuffer(), request.origin)` に変更

### サンプル更新

- **m5stack_basic**: `PoolAllocator` / `PoolAllocatorAdapter` を追加、`renderer.setPivotCenter()` 追加
- **m5stack_matte**: ローカル定義の `PoolAllocatorAdapter` を削除し、fleximg 本体のものを使用

---

## [2.51.0] - 2026-01-25

### 破壊的変更

- **pivot/origin 用語の統一**: SourceNode/NinePatchSourceNode の「画像内基準点」を `pivot` に改名
  - `setOrigin()` → `setPivot()` に変更
  - `originX()`/`originY()` → `pivotX()`/`pivotY()` に変更
  - バインディング: `pivot: {x, y}` オブジェクト形式を推奨（`originX/Y` も後方互換サポート）
  - WebUI: 「原点」ラベルを「基準点 (Pivot)」に変更

- **deprecated 要素の廃止**:
  - `SourceNode`/`NinePatchSourceNode`: `setOrigin()`, `originX()`, `originY()` を削除
  - `int_fixed8` (Q24.8固定小数点): 型、定数、変換関数を全て削除
  - `placeOnto()`: コメントアウト済みの関数を完全削除

### 用語定義

| 用語 | 意味 | 使用箇所 |
|------|------|----------|
| **pivot** | 画像内のアンカーポイント（回転・配置の中心） | SourceNode, NinePatchSourceNode |
| **origin** | バッファ左上のワールド座標 | RenderRequest, RenderResponse, SinkNode, RendererNode |

---

## [2.50.0] - 2026-01-25

### 破壊的変更

- **origin 座標系の統一**: `origin` の意味を「バッファ左上のワールド座標」に統一
  - 旧: SourceNode の origin は「画像内の基準点（pivot）位置」
  - 新: 全ノードで origin は「バッファ[0,0]のワールド座標」を意味
  - RenderRequest/RenderResponse の origin も同様に統一
  - offset 計算: `src.origin - dst.origin`（ワールド座標の差分）

### 座標系変更に伴う修正

- **SourceNode**: origin パラメータの解釈を変更（pivot → ワールド座標）
- **SinkNode**: 配置計算を `originX_ + input.origin.x` 方式に変更
- **LcdSinkNode**: m5stack サンプルの座標計算を SinkNode と統一
- **canvas_utils**: placeFirst/placeUnder のオフセット計算を新座標系に修正
- **NinePatchSourceNode**: パッチ配置の座標計算を修正
- **MatteNode**: 全体の座標計算を新座標系に修正
- **テスト**: blend_test, filter_nodes_test を新座標系に対応

### 機能追加

- **NinePatchSourceNode**: `getDataRange()` オーバーライドを追加
  - 9パッチ全体の有効データ範囲（X方向の和集合）を返す
- **FilterNodeBase**: `getDataRange()` を上流に伝播するオーバーライドを追加

### パフォーマンス改善

- **VerticalBlurNode: バッファサイズ最適化**
  - キャッシュ幅を画面全体ではなく上流AABBに基づいて確保
  - 各行の有効範囲（DataRange）を記録し、出力を最適化
  - `getDataRange()` オーバーライドを追加（上下radius行の和集合を返す）
  - getDataRange/pullProcess 間のキャッシュ機構を導入
  - 出力origin計算をキャッシュ座標系基準に変更し、端数成分による位置ずれを解消

---

## [2.49.0] - 2026-01-24

### パフォーマンス改善

- **SourceNode: getDataRange() のアフィン対応と計算キャッシュ**
  - アフィン変換時、AABBではなくスキャンラインごとの正確なデータ範囲を返すように改善
  - `calcScanlineRange()`: 範囲計算ロジックを共通関数化
  - getDataRange() で計算した結果を pullProcess() で再利用するキャッシュ機構
  - キャッシュキーは origin 座標（無効値は INT32_MIN で表現）

- **CompositeNode: getDataRange() オーバーライド追加**
  - 全上流ノードの getDataRange() 和集合を返すように改善
  - SourceNode の正確な範囲情報が下流に正しく伝播
  - `calcUpstreamRangeUnion()`: 和集合計算ロジックを共通関数化
  - SourceNode と同様のキャッシュ機構を導入

### デバッグ機能

- **RendererNode: DataRange 可視化モード追加**
  - `setDebugDataRange(true)` で有効化
  - マゼンタ: getDataRange() 範囲外（データがないはずの領域）
  - 青: AABBでは範囲内だが getDataRange() で除外された領域
  - 緑: getDataRange() 境界マーカー
  - WebUI: Rendererノード詳細パネルにチェックボックス追加

### 技術詳細

- 下流ノードが getDataRange() → pullProcess() の順で呼び出す際、同一 origin なら計算をスキップ
- 回転時のAABB過大評価を解消し、下流のバッファ確保を最適化可能に
- CompositeNode 経由でも SourceNode の最適化が活用されるように

---

## [2.48.0] - 2026-01-24

### サンプル改善

- **m5stack_basic: 複数Source合成デモ追加**
  - 4つのSourceNodeを使ったCompositeNode合成デモ
  - ボタン操作によるインタラクティブなモード切替
  - CompositeNodeのAffineCapabilityを使用した全体変換デモ

- **デモモード**
  - モード0: 単体回転（従来互換）
  - モード1: 4Source静止表示
  - モード2: Composite全体回転 - 4つの画像が一体となって回転
  - モード3: 個別+全体回転 - 各画像が自転しつつ全体が公転

- **ボタン操作**
  - BtnA: モード切替
  - BtnB: 速度調整（Slow/Normal/Fast）
  - BtnC: 回転方向反転

---

## [2.47.0] - 2026-01-24

### 機能追加

- **WebUI タブ形式詳細ダイアログ**
  - 各ノードの詳細パネルをタブ形式（「基本設定」「アフィン」）に変更
  - `createTabContainer()`: 汎用タブコンテナUIコンポーネント追加
  - `buildAffineTabContent()`: タブ内アフィンコントロールビルダー追加
  - 対応ノード: Image, NinePatch, Sink, Composite, Distributor
  - Affineノード: タブなし、直接アフィンコントロール表示

- **アフィンUI構造の改善**
  - X/Y移動スライダーをモード切替の外に常時表示
  - パラメータモード: 回転・スケールのみ
  - 行列モード: a,b,c,dのみ（tx/tyは上部で管理）
  - タブボタンのスタイルを「パラメータ」「行列」ボタンと統一
  - `getAffineMatrix()`: matrixModeに応じて適切な行列を返すヘルパー関数追加

### バグ修正

- 行列モードでa,b,c,dを変更しても表示に反映されない問題を修正
  - 原因: nodesForCpp生成時に常にパラメータから行列を再計算していた
  - 対策: `getAffineMatrix()` でmatrixModeに応じて適切な行列を返すように変更

- 行列モードでX/Y移動スライダー操作時にa,b,c,dが上書きされる問題を修正
  - 原因: X/Y移動変更時に常にパラメータから行列を再計算していた
  - 対策: `updateTranslation()` ヘルパーで、行列モード時はtx/tyのみ更新

- ノードグラフ上のX/Yスライダーが動作しない問題を修正
  - 原因: 廃止された`position.x/y`を使用していた
  - 対策: `translateX/translateY`に移行し、`matrix.tx/ty`と同期

### 機能改善

- **X/Y倍率スライダの範囲拡張**
  - パラメータモードのX倍率・Y倍率スライダの範囲を0.1〜3から-5〜+5に変更
  - 反転（マイナス値）を含む幅広い変換に対応

### 破壊的変更

- **position 概念の廃止**
  - `node.position.x/y` を廃止し、`matrix.tx/ty` に統一
  - これにより回転・スケールと移動が正しく組み合わせ可能に
  - Image/NinePatchノードの `setPosition()` 呼び出しを削除
  - bindings.cpp: `positionX/positionY` メンバー削除
  - app.js: `createPositionSection()` 関数削除

### バグ修正

- 回転/スケール設定時に配置位置が無視される問題を修正
  - 原因: `setPosition()` の後に `setMatrix()` が上書き
  - 対策: 配置位置を `matrix.tx/ty` で統一管理

---

## [2.46.0] - 2026-01-24

### 機能追加

- **WebUI アフィンコントロール共通化**
  - `createAffineControlsSection()`: 折りたたみ式のアフィン変換UIコンポーネントを新規追加
  - パラメータモード（回転・スケール）と行列モード（a,b,c,d,tx,ty）の切り替えが可能
  - 対応ノード: Image, NinePatch, Sink, Composite, Distributor, Affine
  - 変換設定時はインジケーター（●）で視覚的にフィードバック

- **NinePatchSourceNode に AffineCapability 追加**
  - 9patch画像でもアフィン変換が利用可能に
  - WebUIから回転・スケール設定が可能

### WebUI 変更

- **app.js**:
  - `createAffineControlsSection()` 共通関数追加
  - `hasAffineTransform()` 判定関数追加
  - 各ノードタイプの詳細パネルにアフィンコントロールを統合

- **bindings.cpp**:
  - 全AffineCapability対応ノードで `matrix` オブジェクトをパース
  - `setMatrix()` による変換行列適用を各ノード構築時に実施

---

## [2.45.0] - 2026-01-24

### 機能追加

- **AffineCapability Mixin 導入**: アフィン変換機能を Mixin クラスとして抽出し、複数ノードで共通API提供
  - `setMatrix()`, `setRotation()`, `setScale()`, `setTranslation()`, `setRotationScale()`
  - `hasLocalTransform()` で単位行列判定

- **アフィン機能追加ノード**:
  - **SourceNode**: 既存の `setPosition()` は `setTranslation()` のエイリアスに（後方互換維持）
  - **SinkNode**: 新規アフィン機能追加
  - **CompositeNode**: 合成結果全体の変換が可能（全上流に伝播）
  - **DistributorNode**: 分配先全てに同じ変換を適用（全下流に伝播）
  - **AffineNode**: 既存セッターを Mixin に移行

### 設計

- **Mixin パターン**: `AffineCapability` は Node とは独立したクラスで、多重継承で利用
  - 関心の分離（アフィン機能は Node 階層と独立）
  - 将来の拡張性（他の Capability も追加可能）

---

## [2.44.0] - 2026-01-24

### パフォーマンス改善

- **ピクセルフォーマット変換の最適化**: ESP32向けにtoStraight/fromStraight関数を大幅に高速化

  | フォーマット | 操作 | 改善前 | 改善後 | 改善率 |
  |-------------|------|--------|--------|--------|
  | RGB332 | toStr | 462us | 158us | 66% |
  | RGB332 | frStr | 411us | 167us | 59% |
  | RGB565_LE | toStr | 368us | 274us | 26% |
  | RGB565_BE | toStr | 368us | 274us | 26% |
  | RGB888 | toStr | 325us | 167us | 49% |
  | RGB888 | frStr | 291us | 150us | 48% |
  | BGR888 | toStr | 325us | 167us | 49% |
  | BGR888 | frStr | 308us | 150us | 51% |

### 技術詳細

- **RGB332**: テーブルをuint32_t[256]に変更（768→1024バイト）、32bitロード/ストア活用
- **RGB565**: テーブルをuint16_t[256]に変更、16bitロード/ストア活用、緑成分を上位バイトに統一
- **RGB888/BGR888**: 4ピクセルアンロール、ポインタインクリメント方式でインデックス計算削減
- **ESP32最適化**: ロード順序調整によるレイテンシ隠蔽、シフト+加算命令形式の活用

---

## [2.43.0] - 2026-01-23

### リファクタリング

- **DDA関数をview_ops::に移動**: ViewPort操作の一貫性向上と将来のbit-packed format対応準備
  - `transform::copyRowDDA<BPP>` → `view_ops::copyRowDDA`（BPP分岐内蔵）
  - `transform::copyRowDDABilinear_RGBA8888` → `view_ops::copyRowDDABilinear`
  - `transform::applyAffineDDA` → `view_ops::affineTransform`
  - `transform::calcValidRange`は純粋数学関数としてtransform::に残存

- **APIシグネチャ統一**: DDA関数をViewPort依存APIに統一
  - rawポインタ引数 → ViewPort参照
  - `int32_t` → `int_fixed`（型エイリアス）
  - 引数名をDDA用語に変更（fixedInvA/C → incrX/Y）

### 技術詳細

- 呼び出し元（source_node.h, sink_node.h）のBPP分岐switch文が不要に
- transform.hを大幅削減（313行 → 114行）
- 将来bit-packed formatをViewPortに追加した際、DDA関数が自動的に追加情報を利用可能

---

## [2.42.0] - 2026-01-23

### リファクタリング

- **FLEXIMG_IMPLEMENTATION分離の徹底**: 全ノードファイルで実装部をIMPLEMENTATIONセクションに分離
  - 分離対象: 仮想オーバーライドメソッド、privateヘルパーメソッド
  - ヘッダ部に残すもの: コンストラクタ、短いアクセサ、短いpublicメソッド
  - vtableリンケージ問題を解決し、stb-styleパターンの一貫性を向上

- **分離済みファイル一覧**:
  - `core/node.h`: checkPrepareStatus, convertFormat, initPorts
  - `nodes/vertical_blur_node.h`: onPullProcess, onPushProcess, applyVerticalBlur等
  - `nodes/horizontal_blur_node.h`: onPullProcess, onPushProcess, applyHorizontalBlur
  - `nodes/matte_node.h`: onPullPrepare/Finalize/Process, private helpers
  - `nodes/source_node.h`: onPullPrepare, onPullProcess, pullProcessWithAffine
  - `nodes/ninepatch_source_node.h`: onPull系, calcSrcPatchSizes等
  - `nodes/composite_node.h`: onPullPrepare/Finalize/Process
  - `nodes/renderer_node.h`: execPrepare, execProcess
  - `nodes/sink_node.h`: onPushPrepare/Process, pushProcessWithAffine, applyAffine
  - `nodes/filter_node_base.h`: onPullProcess, process
  - `nodes/affine_node.h`: onPullPrepare/Process, onPushPrepare/Process
  - `nodes/distributor_node.h`: onPushPrepare/Finalize/Process

- **不要な`_IMPL_INCLUDED`ガードを削除**: 11ファイルから冗長なガードを除去

### 技術詳細

- stb-style（Implementation Macro パターン）の完全適用
- `fleximg.cpp`が唯一のコンパイル単位として全実装を含む
- 仮想メソッドの定義をコンパイル単位に配置することでvtable問題を回避

---

## [2.41.0] - 2026-01-23

### 変更

- **Premulモード条件付きコンパイル**: `FLEXIMG_ENABLE_PREMUL` マクロで有効化する方式に変更
  - デフォルト: 8bit Straight合成（省メモリ、C++14互換）
  - Premul有効時: 16bit Premultiplied合成（高精度、C++17必須）
  - `build.sh --premul` でWASM Premulビルド

- **ビルド構成統合**: プロジェクトルートの `platformio.ini` に一本化
  - `examples/m5stack_basic/platformio.ini` 削除
  - `examples/m5stack_matte/platformio.ini` 削除
  - `examples/m5stack_benchmark/` を `examples/bench/` に統合

### ドキュメント

- DESIGN_PIXEL_FORMAT.md: Premul仕様の記述を更新
- ARCHITECTURE.md: フォーマット表を更新
- BENCHMARK_BLEND_UNDER.md: ビルドコマンドを更新
- README.md: ビルド手順を更新

---

## [2.40.0] - 2026-01-22

### パフォーマンス改善

- **blendUnderPremul関数の大幅最適化**: 最大25%の性能改善
  - SWAR (SIMD Within A Register): RG/BAを32bitにパックして同時演算
  - 8bit精度アルファ処理: 16bit→8bit変換でシフト演算を削減
  - プリデクリメント方式: continue時のポインタ進行を保証
  - 分岐構造の最適化: ストア処理を統合しコードパスを単純化

- **toPremul関数のSWAR最適化**: 約19%の性能改善
  - RG/BAを32bitにパックして乗算を4回→2回に削減
  - 4ピクセルアンローリングとの組み合わせで効果最大化

- **ベンチマーク結果**:
  - blendUnderPremul (Semi/Sem): 1388us → 1045us (-24.7%)
  - toPremul: 368us → 299us (-18.8%)
  - Compositeシナリオ全体: 43.8fps → 45.1fps (+3.0%)

### 追加

- **m5stack_method_bench**: 4×4マトリクステスト追加
  - src×dstパターン総当たりでblendUnderPremulの条件分岐影響を測定

---

## [2.39.0] - 2026-01-21

### バグ修正

- **PoolAllocator連続ブロック解放バグ修正**: 複数ブロック確保時の解放処理を修正
  - 確保ブロック数を`blockCounts_[32]`配列で記録
  - 解放時に正しいブロック数分のビットをクリア
  - フラグメンテーション軽減のため探索方向を交互に切り替え

### パフォーマンス改善

- **ESP32向け計測コード最適化**: `std::chrono`から`micros()`に置き換え
  - `std::chrono::high_resolution_clock`はESP32で非常に遅い
  - `micros()`使用で計測オーバーヘッドを大幅削減（+45%改善）

- **リリースビルドのメトリクス呼び出し削減**
  - `renderer_node.h`の`PerfMetrics::instance().reset()`を条件付きに変更
  - リリースビルドでの不要なシングルトンアクセスを回避

- **blendUnderPremulに`__restrict__`追加**
  - コンパイラ最適化のヒントを提供

### 追加

- **ベンチマークにFormatMetrics表示追加**: フォーマット変換統計の可視化
  - フォーマット別・操作別の呼出回数とピクセル数を出力
  - 最適化ポイントの特定に有用

### 変更

- **プールサイズ調整**: 1KB×32ブロック（合計32KB）に変更
  - より細かい粒度でのメモリ割り当てに対応
- **perf_metrics.hの型統一**: リリース版`totalNodeAllocatedBytes()`を`uint32_t`に統一

---

## [2.38.0] - 2026-01-21

### パフォーマンス改善

- **ピクセルフォーマット変換関数の最適化**: ESP32向けに大幅な高速化
  - 逆数テーブル（除算→乗算変換）
  - `__restrict__` + ポインタ算術
  - 4ピクセルアンロール
  - アルファ上位バイト直接読み取り（リトルエンディアン最適化）
  - ESP32 shift-add専用命令の活用（`|` → `+`）
  - マクロによるコード簡潔化

ベンチマーク結果（M5Stack CoreS3、4096ピクセル×1000回）：

| 関数 | 最適化前 | 最適化後 | 改善率 |
|------|----------|----------|--------|
| rgb565be_fromPremul | 732us | 415us | **43.3%** |
| rgb565le_toPremul | 548us | 351us | **35.9%** |
| rgb565le_fromPremul | 637us | 432us | **32.2%** |
| rgb565be_fromStraight | 411us | 287us | **30.2%** |
| rgb565le_fromStraight | 411us | 282us | **31.4%** |
| rgba8_fromPremul | 612us | 458us | **25.2%** |
| rgb565be_toPremul | 599us | 454us | **24.2%** |
| rgba8_toPremul | 462us | 368us | **20.3%** |

### 追加

- **単体メソッドベンチマーク**: `examples/m5stack_method_bench/`
  - 変換関数の単体性能測定用プロジェクト
  - M5Stack Core2/CoreS3対応

---

## [2.37.0] - 2026-01-21

### 追加

- **Under合成（blendUnderPremul）**: CompositeNodeの合成方式を改善
  - `BlendParams` 構造体: グローバルアルファ等のパラメータを保持
  - `PixelFormatDescriptor::blendUnderPremul`: 各フォーマット用のunder合成関数
  - 対応フォーマット: RGBA8, RGB565_LE/BE, RGB332, RGB888, BGR888, Alpha8
  - 入力画像を標準形式に変換せず直接合成（メモリ効率・処理速度向上）

- **フォーマット変換メトリクス（FormatMetrics）**: 変換関数の呼び出し統計
  - `FormatMetrics` シングルトン: フォーマット×操作タイプ別の統計
  - `FLEXIMG_FMT_METRICS` マクロ: 各変換関数で計測
  - 操作タイプ: ToStandard, FromStandard, BlendUnder, FromPremul
  - WebUI: デバッグセクションに「フォーマット変換」表示を追加
  - snapshot/restore機能: UI用変換をメトリクスから除外

### 変更

- **CompositeNode**: over合成からunder合成に変更
  - 背景をPremultiplied形式のキャンバスで管理
  - 各入力を「下に敷く」形で順次合成

### ドキュメント

- **DESIGN_PERF_METRICS.md**: FormatMetricsセクションを追加
- **DESIGN_PIXEL_FORMAT.md**: Under合成（blendUnderPremul）セクションを追加

---

## [2.36.0] - 2026-01-20

### 変更

- **内部フォーマットをRGBA8_Straightに統一**: RGBA16_Premultipliedを無効化
  - `canvas_utils::createCanvas()`: RGBA8_Straight固定
  - `blend.cpp`: RGBA8_Straight同士のブレンドパスのみ有効
  - 逆数テーブルによる除算回避最適化を追加
  - dstA==255の場合の特殊処理パスを追加

### 無効化（`#if 0`で封印）

- **RGBA16_Premultiplied関連コード**:
  - `pixel_format.h`: RGBA16Premul閾値、BuiltinFormats宣言、直接変換テーブル
  - `pixel_format.cpp`: 変換関数、Descriptor定義
  - `blend.cpp`: RGBA8→RGBA16変換パス、16bit同士のブレンドパス

### パフォーマンス改善

ベンチマーク結果（M5Stack Core2、320x220描画領域）：

| シナリオ | 旧FPS | 新FPS | 改善率 | メモリ削減 |
|---------|-------|-------|--------|-----------|
| Source | 29.9 | 34.2 | +14% | - |
| Affine | ~30 | 34.3 | +14% | - |
| Composite | 11.6 | 16.2 | **+40%** | **-33%** |
| Matte | 12.3 | 12.5 | +2% | -5% |

### 技術詳細

- フォーマット変換オーバーヘッドの削減が主な改善要因
- メモリ帯域幅とキャッシュ効率の向上（4バイト vs 8バイト/ピクセル）
- RGBA8ブレンドの逆数テーブル最適化は効果が限定的（不透明背景パスが支配的）

---

## [2.35.0] - 2026-01-19

### 追加

- **M5Stack サンプル** (`examples/m5stack_basic/`)
  - M5Stack Core2/CoreS3 向けの回転矩形デモ
  - PlatformIO プロジェクト（native/Core2/CoreS3 ターゲット）
  - `LcdSinkNode`: M5GFX経由でLCDにスキャンライン転送するカスタムSinkNode
  - RGBA8_Straight → RGB565_BE (swap565_t) フォーマット変換
  - マージンクリア機能（画像部分を避けて左右余白のみ消去）

### 変更

- **RendererNode**: 有効なデータがない場合でも常に下流にpushProcessを呼び出すように変更
  - 空のスキャンラインでもSinkNodeで適切な背景消去が可能に

---

## [2.34.0] - 2026-01-18

### 追加

- **ブラーフィルタ: ガウシアンブラー近似機能**
  - HorizontalBlurNode と VerticalBlurNode に `passes` パラメータを追加（1-5回、デフォルト1）
  - passes=3で3回ボックスブラーを適用し、ガウシアン分布に近似
  - 中心極限定理により、複数回のボックスブラーでガウシアンブラーを近似
  - Stack Blur（以前の試み）と比較して4倍高速、アルゴリズムも安定

- **WebUI: 複数フィルタパラメータ対応**
  - ノードボックスに複数のスライダーを表示（radius + passes）
  - 詳細ダイアログでも複数パラメータを編集可能
  - 古い形式（node.param）からの自動移行処理を実装

### 修正

- **HorizontalBlurNode: pull型の正しいorigin処理**
  - DESIGN_RENDERER_NODE.mdの仕様に準拠し、出力拡張方式に変更
  - 上流への要求サイズを変更せず、各パスで拡張
  - 出力: width = 入力 + radius*2*passes, origin.x = 入力origin.x + radius*passes
  - inputOffset = -radius に修正し、描画内容のずれを解消

- **VerticalBlurNode: マルチパス処理の実装**
  - passes > 1 の場合、重み付きカーネルを使用
  - 畳み込みカーネルを事前計算し、ガウシアン近似を実現
  - pull型とpush型の両方に対応

### 技術詳細

- **ガウシアン近似の理論**:
  ```
  radius=5, passes=3の場合:
  - 1パス: ボックスフィルタ [1,1,1,1,1,1,1,1,1,1,1]
  - 2パス: 三角分布に近い重み
  - 3パス: ガウシアン分布に近似（中心極限定理）

  最終カーネルサイズ: 2*radius*passes + 1 = 31
  ```

- **pull型origin処理** (horizontal_blur_node.h:79-133):
  ```cpp
  // 各パスで拡張
  for (int pass = 0; pass < passes_; pass++) {
      int outputWidth = inputWidth + radius_ * 2;
      applyHorizontalBlur(srcView, -radius_, output);
      currentOrigin.x = currentOrigin.x + to_fixed(radius_);
  }
  return RenderResponse(std::move(buffer), currentOrigin);
  ```

- **マルチパスカーネル計算** (vertical_blur_node.h:367-388):
  ```cpp
  std::vector<int> computeMultiPassKernel(int radius, int passes) {
      std::vector<int> kernel(2 * radius + 1, 1);
      for (int p = 1; p < passes; p++) {
          // 畳み込みを繰り返してガウシアン近似
          std::vector<int> newKernel(kernel.size() + singleSize - 1, 0);
          for (size_t i = 0; i < kernel.size(); i++) {
              for (int j = 0; j < singleSize; j++) {
                  newKernel[i + j] += kernel[i];
              }
          }
          kernel = std::move(newKernel);
      }
      return kernel;
  }
  ```

---

## [2.33.1] - 2026-01-17

### 修正

- **WebUI: スマートフォンでの操作性を改善**
  - 長押しコンテキストメニューが確実に表示されるように修正
    - 移動距離の閾値判定（10px）を導入
    - タッチ移動フラグで長押しとドラッグを明確に区別
    - 移動していない場合は500ms後にコンテキストメニューを表示
  - タップ後にノードが意図せず移動する問題を修正
    - handleEnd内でisDraggingの状態に関わらず、常にイベントリスナーを削除
    - タップ後のリスナーが残り続けることによるメモリリークも防止
  - メインコンテンツが画面下部にはみ出る問題を修正
    - 100vhを100dvh（Dynamic Viewport Height）に変更
    - -webkit-fill-availableでiOS Safari対応
    - モバイルブラウザのアドレスバー等を考慮した高さ設定

- **テスト: pixel_format_test.cppのコンパイルエラーを修正**
  - std::string使用時に<string>ヘッダーが不足していた問題を解決

### 技術詳細

- setupGlobalNodeDrag関数の改善 (app.js:3073-3198):
  ```javascript
  const DRAG_THRESHOLD = 10; // ドラッグ判定閾値
  let touchMoved = false;    // タッチ移動フラグ

  // 移動距離を計算して閾値以上ならドラッグ開始
  const distance = Math.sqrt(dx * dx + dy * dy);
  if (!isDragging && distance > DRAG_THRESHOLD) {
      touchMoved = true;
      isDragging = true;
      // 長押しタイマーをキャンセル
  }
  ```

- .containerの高さ設定 (style.css:20, 1573):
  ```css
  height: calc(100vh - Xpx);                /* フォールバック */
  height: calc(100dvh - Xpx);               /* Dynamic Viewport Height */
  height: -webkit-fill-available;           /* iOS Safari対応 */
  max-height: calc(100dvh - Xpx);           /* はみ出し防止 */
  ```

---

## [2.33.0] - 2026-01-16

### 修正

- **NinePatchSourceNode: 伸縮部の位置ずれを修正**
  - コード簡略化時に誤ったオーバーラップロジックが導入されていた問題を修正
  - 誤: 隣接パッチがある方向に拡張（中央伸縮部が左に拡張される）
  - 正: 固定部→伸縮部方向に拡張（中央伸縮部は拡張なし）

- **NinePatchSourceNode: クリッピング時の固定部処理を改善**
  - 出力サイズが固定部の合計より小さい場合の処理を変更
  - 従来: 固定部を内側から切り詰め（ceil による離散的なサイズ変化でカクカクした動き）
  - 修正後: 固定部を縮小スケーリング（連続的なスケール変化で滑らかな動き）
  - クリッピング状態の判定も出力サイズベースに変更

- **WebUI: NinePatchノードのID重複問題を修正**
  - 状態復元後にNinePatchを追加すると、既存ノードとIDが重複する問題を修正
  - `nextNinePatchNodeId` が状態の保存・復元から漏れていたことが原因
  - 複数のNinePatchノードで同じポートが反応する現象が解消

- **SourceNode: position がアフィン変換に追従するように修正**
  - CompositeNode 配下の複数ソースで position を設定した際、上流の AffineNode の変換（回転・拡縮・せん断）が各ソースの相対位置関係に正しく反映されるように修正
  - 従来: position がアフィン行列の tx/ty に直接加算されていたため、位置関係がずれていた
  - 修正後: position をアフィン行列の 2x2 部分で変換してから加算

### 技術詳細

- SourceNode の `pullPrepare()` での変換:
  ```cpp
  // 修正前: mat.tx += positionX_; mat.ty += positionY_;
  // 修正後:
  float transformedPosX = mat.a * positionX_ + mat.b * positionY_;
  float transformedPosY = mat.c * positionX_ + mat.d * positionY_;
  mat.tx += transformedPosX;
  mat.ty += transformedPosY;
  ```

- NinePatchSourceNode は既に行列乗算 (`request.affineMatrix * patchScales_[i]`) で処理していたため修正不要

---

## [2.32.0] - 2026-01-16

### 追加

- **水平/垂直ブラーノードの分離**: BoxBlurを水平・垂直の独立したノードに分離
  - `HorizontalBlurNode`: 水平方向のみのブラー処理（1行完結、キャッシュ不要）
  - `VerticalBlurNode`: 垂直方向のみのブラー処理（列合計ベース）
  - 組み合わせにより従来のBoxBlurと同等の結果を得られる
  - モーションブラー的な効果（方向別ブラー）に対応

### 修正

- **HorizontalBlurNode**: pull型処理での`inputOffset`計算を修正（座標系の符号を訂正）
- **HorizontalBlurNode**: push型処理で出力幅を`入力幅+radius*2`に拡張（BoxBlurNodeと同様）
- **VerticalBlurNode**: push型処理での複数の問題を修正
  - 出力タイミングを`radius`行遅延させて正しいブラー結果を得るように修正
  - `origin.y`計算をBoxBlurNodeと同様の方式に修正
  - アフィン変換された画像（行ごとにorigin.xが異なる）に対応
  - キャッシュ格納時のX座標アライメントを修正（座標系の符号を訂正）
- **SourceNode**: 180度回転が適用されない問題を修正
  - `isDotByDot`判定を厳格化（単位行列のみ非アフィンパス使用）
  - 反転を含む変換は正しくアフィンパスで処理されるように修正

### リファクタリング

- **HorizontalBlurNode**: `applyHorizontalBlur`と`applyHorizontalBlurPush`を統合
- **VerticalBlurNode**: 列合計からの出力計算を`writeOutputRowFromColSum`に抽出

### 使用例

```cpp
// 水平ブラーのみ
src >> hblur >> renderer >> sink;

// 垂直ブラーのみ
src >> vblur >> renderer >> sink;

// 組み合わせ（BoxBlur相当）- HorizontalBlur → VerticalBlur の順が効率的
src >> hblur >> vblur >> renderer >> sink;
```

### WebUI

- フィルタメニューに「水平ぼかし」「垂直ぼかし」を追加
- 既存の「ぼかし」(BoxBlur) も引き続き利用可能
- **デバッグメトリクス表示の改善**
  - ノード名を日本語化（アフィン、ぼかし、水平ぼかし など）
  - Distributor、Source、NinePatchの処理時間を表示に追加
  - NODE_TYPESにnameJaフィールドを追加
  - SourceとNinePatchをsourceカテゴリに整理
  - **計測の重複問題を修正**: 各ノードが自身の処理時間のみを計測するように変更
    - RendererNode: 計測を削除（自身の処理がほぼないため）
    - HorizontalBlurNode, VerticalBlurNode, BoxBlurNode: pullProcess後に計測開始
    - AffineNode: AABB分割処理でapplyAffineのみを計測

---

## [2.31.0] - 2026-01-16

### 変更

- **固定小数点型の Q16.16 統一**: 座標系を Q24.8 から Q16.16 に統一
  - `int_fixed` を `int_fixed16` (Q16.16) のエイリアスとして定義
  - `Point` 構造体を Q16.16 ベースに変更
  - 全ノード・operations の座標計算を Q16.16 に統一
  - 32bit組み込み環境での型変換削減と一貫性向上

### 追加

- **統一変換関数**: `to_fixed()`, `from_fixed()`, `float_to_fixed()`, `fixed_to_float()`
  - Q16.16 統一型用の変換関数を追加
  - 新規コードでは Q24.8 (`int_fixed8`) ではなくこれらを使用

### 非推奨

- **int_fixed8 (Q24.8)**: 後方互換性のため残存、新規コードでは非推奨
  - `to_fixed8()`, `from_fixed8()` は `to_fixed()`, `from_fixed()` に置き換え

### 修正

- **NinePatch 表示問題**: isDotByDot 判定でアフィン行列の平行移動を考慮
  - スケール=1.0 でも tx/ty が設定されている場合、アフィンパスを使用するよう修正

---

## [2.30.0] - 2026-01-14

### 決定仕様

- **スキャンライン必須制約**: パイプライン上を流れるリクエストは必ず高さ1ピクセル（スキャンライン）
  - `RendererNode::effectiveTileHeight()` は TileConfig の設定に関わらず常に 1 を返す
  - この制約により、DDA処理や範囲計算の最適化が可能に
  - 各ノードは height=1 を前提とした効率的な実装が可能

### 追加

- **SourceNode スキャンライン最適化**: アフィン変換付きプル処理の効率化
  - `pullProcessWithAffine()` をスキャンライン専用に最適化
  - 有効ピクセル範囲のみのバッファを返す（範囲外の0データを下流に送らない）
  - `transform::calcValidRange()` で1行の有効範囲を事前計算
  - origin を有効範囲に合わせて調整

### 変更

- **RendererNode タイル高さ固定**: `effectiveTileHeight()` を常に 1 を返すように変更
  - TileConfig の tileHeight 設定は無視される（後方互換性のため設定API自体は維持）
  - tileWidth のみがタイル分割に影響

### 技術詳細

- SourceNode の pullProcessWithAffine():
  ```cpp
  // 1行の有効範囲を計算
  auto [xStart, xEnd] = transform::calcValidRange(invMatrix_.a, rowBaseX, source_.width, request.width);
  auto [yStart, yEnd] = transform::calcValidRange(invMatrix_.c, rowBaseY, source_.height, request.width);

  // 有効範囲のみのバッファを作成
  int validWidth = dxEnd - dxStart + 1;
  ImageBuffer output(validWidth, 1, source_.formatID);
  ```

- RendererNode の effectiveTileHeight():
  ```cpp
  int effectiveTileHeight() const {
      // スキャンライン必須（height=1）
      return 1;
  }
  ```

---

## [2.29.0] - 2026-01-13

### 変更

- **フォルダ構造の整理**: `core/`, `image/` サブディレクトリを導入
  - `core/`: コア機能（types, node, port, perf_metrics）
  - `image/`: 画像処理（viewport, image_buffer, pixel_format）
  - `nodes/`, `operations/` は既存のまま維持

- **namespace core の導入**: `core/` 内ファイルを `fleximg::core` 名前空間に移行
  - 後方互換性のため親名前空間に using 宣言（将来廃止予定）
  - 新規コードでは `core::` プレフィックスを使用

- **AffineMatrix の移動**: `common.h` から `types.h` に移動
  - `common.h` は NAMESPACE 定義とバージョン情報のみに簡略化

### 追加

- **汎用メモリアロケータ** (`core/memory/`): 組込み環境対応のメモリ管理
  - `IAllocator`: アロケータインターフェース
  - `DefaultAllocator`: 標準 malloc/free ラッパー
  - `IPlatformMemory`: プラットフォーム固有メモリ（SRAM/PSRAM選択）
  - `PoolAllocator`: ビットマップベースのプールアロケータ
  - `BufferHandle`: RAII方式のバッファハンドル

- **ImageBuffer の新アロケータ対応**: `core::memory::IAllocator` を使用
  - 内部実装を `ImageAllocator*` から `core::memory::IAllocator*` に変更

### 非推奨

- **image/image_allocator.h**: `core/memory/allocator.h` に置き換え
  - `ImageAllocator` → `core::memory::IAllocator`
  - `DefaultAllocator` → `core::memory::DefaultAllocator`
  - 将来のバージョンで削除予定

---

## [2.28.0] - 2026-01-12

### 追加

- **複数Sink同時レンダリング**: 1回のrender passで全Sinkノードを更新
  - DistributorNode経由で複数Sinkに分岐する構成をサポート
  - C++: 全Sinkを収集し、各SinkNodeにバッファを作成
  - JS: 全Sinkの出力バッファを確保してからevaluateGraph()

### 変更

- **Sinkノード作成時の原点**: コンテンツの中央 (width/2, height/2) をデフォルトに

### 修正

- **初期化時のRenderer設定同期**: ページ読み込み直後から正しく表示
  - `restoreAppState()`: Rendererノードの設定をC++側に同期
  - `initDefaultState()`: 同様の同期処理を追加
  - 仮想スクリーンサイズ、原点、タイル設定を全て反映

### 技術詳細

- bindings.cpp: `buildAndExecute()` で全Sinkを収集・処理
- bindings.cpp: `sinkNodeMap` でSinkノードをID管理
- app.js: `updatePreviewFromGraph()` で全Sink結果をcontentLibraryに保存
- `setTargetSink()` APIは残存（将来の単一Sink最適化用）

---

## [2.27.0] - 2026-01-12

### 追加

- **DistributorNode**: 1入力・N出力の分配ノードを新設
  - CompositeNode（N入力・1出力）と対称的な構造
  - 下流には参照モードImageBuffer（ownsMemory()==false）を渡す
  - 下流ノードが変更を加えたい場合はコピーを作成
  - WASMバインディング対応（distributor タイプ）
  - WebUI対応: ノード追加メニュー、詳細パネル、コンテキストメニュー

### WebUI機能

- ノード追加メニュー: 「合成」カテゴリに「分配」ノードを追加
- 詳細パネル: 出力数表示と「+ 出力を追加」ボタン
- コンテキストメニュー: 分配ノードで「➕ 出力を追加」を表示
- 動的出力ポート: CompositeNodeの入力ポートと対称的な構造
- 状態保存/復元: nextDistributorIdの永続化

### 技術詳細

- NodeType に Distributor を追加（インデックス再配置）
- pushPrepare/pushFinalize: 全下流ノードへ伝播
- pushProcess: 参照モードで配信、最後の出力にはmoveで渡す
- bindings.cpp: distributor ノードタイプの解析・構築処理

---

## [2.26.0] - 2026-01-12

### 修正

- **AffineNode: タイル境界での画像抜け問題を修正**
  - アフィン変換 + タイル分割 + 拡大の組み合わせで発生していた1pixel幅の画像抜けを解消
  - `computeInputRegion()` の AABB 計算を数学的に正確な方法に修正

### 技術詳細

- 原因: ピクセル境界座標を逆変換後に ±0.5 補正していたが、アフィン変換後の補正では不正確
- 解決: DDA がサンプリングする**ピクセル中心座標**を逆変換前に使用
  - corners（台形フィット用）: ピクセル境界座標のまま維持
  - AABB（入力要求領域）: ピクセル中心座標 (0.5, width-0.5) から計算
- 結果: 100% の正確性、余分なピクセル要求なし

---

## [2.25.0] - 2026-01-12

### 追加

- **AffineNode 出力フィット**: 有効ピクセル範囲のみを返すように最適化
  - process() と pullProcessWithAABBSplit() で AffineResult を使用
  - 有効範囲が全域より小さい場合、トリミングして返す
  - 有効ピクセルがない場合は空のバッファを返す（透明扱い）
  - 非アルファフォーマット（RGB565等）でアフィン変換時の背景黒問題を軽減

### 技術詳細

- AffineResult 構造体: applyAffine() の返り値で実際に書き込んだピクセル範囲を取得
  - minX, maxX, minY, maxY: DDA ループで追跡した有効範囲
  - isEmpty(), width(), height(): ヘルパーメソッド

---

## [2.23.0] - 2026-01-12

### 修正

- **CompositeNode: 異なるピクセルフォーマットの合成対応**
  - RGB565, RGB888 等の入力を RGBA16_Premultiplied に自動変換
  - blend関数が対応していないフォーマットでも正しく合成可能に
  - ImageBuffer::toFormat() を行単位変換に修正（サブビューのストライド対応）

---

## [2.22.0] - 2026-01-11

### 追加

- **Sinkノード機能拡張**: 出力フォーマット選択・サムネイル・複数Sink対応準備
  - `SinkOutput` 構造体: Sink別出力バッファ管理
  - `setSinkFormat(sinkId, formatId)`: Sink別出力フォーマット設定API
  - `getSinkPreview(sinkId)`: Sink別プレビュー取得API（RGBA8888変換）
  - WebUI: Sinkノード詳細パネルにフォーマット選択UI追加
  - WebUI: Sinkノード内に出力サムネイル表示を追加
  - 指定フォーマットで内部バッファに保存、Canvas表示用に自動変換

---

## [2.21.0] - 2026-01-11

### 追加

- **多ピクセルフォーマット対応 Phase 1-3**: 組み込み向けフォーマットの基盤整備
  - RGB565_LE/BE: 16bit RGB（リトル/ビッグエンディアン）
  - RGB332: 8bit RGB（3-3-2）
  - RGB888/BGR888: 24bit RGB（メモリレイアウト順）
  - `storeImageWithFormat()`: バインディング層でのフォーマット変換API
  - WebUI: 画像ノード詳細パネルにフォーマット選択UI追加

### 変更

- `getBytesPerPixel()` を `pixel_format_registry.h` に移動
  - PixelFormatDescriptor を参照する実装に変更
  - 全フォーマットに対応（旧実装はRGBA8/RGBA16のみ）

### 技術的詳細

- ビット拡張パターン（除算なし、マイコン最適化）
  - 2bit→8bit: `v * 0x55`
  - 3bit→8bit: `(v * 0x49) >> 1`
  - 5bit→8bit: `(v << 3) | (v >> 2)`
  - 6bit→8bit: `(v << 2) | (v >> 4)`

---

## [2.20.0] - 2026-01-11

### 改善

- **computeInputRegion のマージン最適化**: AABB計算の精度向上
  - ピクセル中心補正を正確に適用
  - 余分なマージンを約99%削減（93.20 px → 0.85 px）
  - スキャンライン分割 + 単位行列で、上流要求が正しく1行に

### 技術的詳細

- DDA サンプリング位置 (dx+0.5, dy+0.5) を正確にモデル化
- corners はピクセル境界座標で計算（台形フィット用）
- AABB 計算時に 0.5 ピクセル補正を適用
- max 側の計算を `ceil` から `floor` に修正

### テスト

- `test/affine_margin_test.cpp`: マージン検証テストを追加
  - 72 角度 × 6 スケール × 25 平行移動 × 4 サイズ = 43,200 ケース
  - DDA シミュレーションで実際のアクセス範囲を検証

---

## [2.19.0] - 2026-01-11

### 追加

- **AffineNode::pushProcess()**: Renderer下流でのアフィン変換をサポート
  - 入力画像の4隅を順変換して出力AABBを計算
  - 変換後のサイズで出力バッファを作成
  - 正しい origin（バッファ内基準点位置）を計算して下流へ渡す
  - タイル分割 + 回転の組み合わせで画像が欠ける問題を解決

### 技術的詳細

- プル型（上流→Renderer）: 逆変換で入力領域を計算
- プッシュ型（Renderer→下流）: 順変換で出力領域を計算
- `fwdMatrix_`（順変換行列）を使用して出力4隅を計算

### 既知の制限

- プッシュ型アフィン後のSinkNode配置でアルファ合成が必要な場合がある
  - 現在はコピー配置のみ（SinkNode側の将来課題）

---

## [2.18.0] - 2026-01-11

### 削除

- **マイグレーションAPI**: 固定小数点移行用の一時APIを削除
  - `SourceNode::setOriginf()`
  - `SinkNode::setOriginf()`
  - `RendererNode::setVirtualScreenf()`
  - `Point(float, float)` コンストラクタ
  - `Point::xf()`, `Point::yf()` アクセサ
  - `Point2f` エイリアス

- **TransformNode**: 非推奨クラスを削除（`transform_node.h`）
  - 代替: `AffineNode`（`affine_node.h`）

### 変更

- **bindings.cpp**: `float_to_fixed8()` を使用して型変換
  - JS境界でのfloat→固定小数点変換をbindings側で実施

### ドキュメント

- **README.md**: ドキュメントガイドセクション追加、テスト実行方法追加
- **docs/README.md**: 設計ドキュメントの入り口を新規作成
- **ARCHITECTURE.md, DESIGN_RENDERER_NODE.md**: TransformNode → AffineNode に更新

---

## [2.17.0] - 2026-01-11

### 追加

- **filters::boxBlurWithPadding()**: 透明拡張ボックスブラーAPI
  - 入力画像の範囲外を透明（α=0）として扱う
  - α加重平均: 透明ピクセルの色成分が結果に影響しない
  - スライディングウィンドウ方式: O(width×height) でradius非依存の計算量

### 変更

- **BoxBlurNode**: 透明拡張ブラーを使用
  - 画像境界でのぼかし効果が自然にフェードアウト
  - タイル分割時の境界断絶問題を解決
  - inputReq サイズで作業バッファを確保し、正しいマージン処理

### 修正

- 画像境界でブラー効果が切断される問題を修正
- タイル分割 + 交互スキップ時にスキップ領域へはみ出す問題を修正

---

## [2.16.0] - 2026-01-11

### 追加

- **ImageBuffer 参照モード**: メモリを所有しない軽量な参照
  - `explicit ImageBuffer(ViewPort view)`: 外部ViewPortを参照するコンストラクタ
  - `ownsMemory()`: メモリ所有の有無を判定
  - `subBuffer(x, y, w, h)`: サブビューを持つ参照モードImageBufferを作成

- **FormatConversion enum**: toFormat()の変換モード
  - `CopyIfNeeded`: 参照モードならコピー作成（デフォルト、編集用）
  - `PreferReference`: フォーマット一致なら参照のまま返す（読み取り専用用）

- **Node::convertFormat()**: フォーマット変換ヘルパー（メトリクス記録付き）
  - 参照→所有モード変換時にノード別統計へ自動記録
  - 各ノードのprocess()がすっきり、#ifdef分岐を集約

- **Node::nodeTypeForMetrics()**: メトリクス用ノードタイプ（仮想メソッド）

### 変更

- **toFormat()**: FormatConversion引数を追加
  - 参照モード + CopyIfNeeded: コピー作成して所有モードに
  - 参照モード + PreferReference: 参照のまま返す

- **コピーコンストラクタ/代入**: 参照モードからもディープコピー（所有モードになる）

- **SourceNode**: サブビューの参照モードで返すように変更
  - データコピーを削減（下流で編集が必要になるまで遅延）

- **フィルタノードのインプレース編集対応**:
  - BrightnessNode, GrayscaleNode, AlphaNode: `convertFormat()` + インプレース編集
  - BoxBlurNode: `convertFormat(..., PreferReference)` で読み取り専用参照
  - 上流でコピー作成、下流は追加確保なしでインプレース編集

---

## [2.15.0] - 2026-01-11

### 追加

- **InitPolicy**: ImageBuffer初期化ポリシー
  - `Zero`: ゼロクリア（デフォルト、既存動作維持）
  - `Uninitialized`: 初期化スキップ（全ピクセル上書き時に使用）
  - `DebugPattern`: デバッグ用パターン値（0xCD, 0xCE, ...）で埋める

### 変更

- **ImageBuffer コンストラクタ**: InitPolicy パラメータを追加
  - `ImageBuffer(w, h, fmt, InitPolicy::Uninitialized)` で初期化スキップ
  - コピーコンストラクタ/代入: Uninitialized を使用（copyFromで上書き）
  - toFormat(): Uninitialized を使用（変換で全ピクセル上書き）

- **各ノードで InitPolicy::Uninitialized を適用**:
  - SourceNode: view_ops::copyで全領域コピー
  - BrightnessNode: filters::brightnessで全ピクセル上書き
  - GrayscaleNode: filters::grayscaleで全ピクセル上書き
  - AlphaNode: filters::alphaで全ピクセル上書き
  - BoxBlurNode: filters::boxBlur + croppedバッファ

---

## [2.14.0] - 2026-01-11

### 変更

- **AffineNode DDA転写ループのテンプレート化**
  - `copyRowDDA<BytesPerPixel>`: データサイズ単位で転写するテンプレート関数
  - ピクセル構造を意識しない汎用的な設計
  - 1, 2, 3, 4, 8 バイト/ピクセルに対応（RGB888等の将来対応）
  - stride 計算をバイト単位に統一
  - 関数ポインタによるディスパッチ（ループ外で1回だけ分岐）
  - 組み込みマイコン環境を考慮し、ループ内の分岐を排除

---

## [2.13.0] - 2026-01-11

### 追加

- **AABB分割 台形フィット**: アフィン変換の入力要求を大幅に削減
  - `computeXRangeForYStrip()`: Y範囲に対応する最小X範囲を計算
  - `computeYRangeForXStrip()`: X範囲に対応する最小Y範囲を計算
  - 各stripを平行四辺形の実際の幅にフィット
  - 45度回転時のピクセル効率が約1% → 約50%に改善

### 変更

- **pullProcessWithAABBSplit()**: 台形フィットを適用
  - Y方向分割時: 各stripのX範囲を平行四辺形にフィット
  - X方向分割時: 各stripのY範囲を平行四辺形にフィット
  - AABB範囲にクランプして安全性を確保

### ドキュメント

- **IDEA_AFFINE_REQUEST_SPLITTING.md**: ステータスを「実装済み（効果検証中）」に更新

---

## [2.12.0] - 2026-01-11

### 追加

- **from_fixed8_ceil() 関数**: 正の無限大方向への切り上げ
  - `types.h` に追加
  - `from_fixed8_floor()` と対になる関数

### 変更

- **AffineNode::computeInputRequest() 精度向上**
  - 全座標を Q24.8 固定小数点で計算（従来は整数）
  - tx/ty の小数部を保持
  - floor/ceil で正確な AABB 境界を計算
  - マージン: +5 → +3 に削減（40%削減）
  - 将来の入力要求分割（IDEA_AFFINE_REQUEST_SPLITTING）の準備

---

## [2.11.0] - 2026-01-11

### 追加

- **Matrix2x2<T> テンプレート**: 2x2行列の汎用テンプレート型
  - `types.h` に追加
  - `Matrix2x2_fixed16`: int_fixed16 版エイリアス
  - 逆行列か順行列かは変数名で区別（型名には含まない）

- **inverseFixed16() 関数**: AffineMatrix → Matrix2x2_fixed16 変換
  - `common.h` に追加
  - 2x2 部分のみ逆行列化、tx/ty は含まない

### 変更

- **AffineNode**: `Matrix2x2_fixed16` を使用するよう移行
  - `invMatrix_` の型を `FixedPointInverseMatrix` → `Matrix2x2_fixed16` に変更
  - `prepare()` で `inverseFixed16()` を使用
  - `INT_FIXED16_SHIFT` を使用（`transform::FIXED_POINT_BITS` から移行）

### 非推奨（次バージョンで削除予定）

- **FixedPointInverseMatrix** (`transform.h`): Matrix2x2_fixed16 + inverseFixed16() に置き換え
- **FIXED_POINT_BITS / FIXED_POINT_SCALE**: INT_FIXED16_SHIFT / INT_FIXED16_ONE に置き換え

---

## [2.10.0] - 2026-01-11

### 追加

- **calcValidRange 関数**: DDA有効範囲を事前計算する独立関数
  - `transform::calcValidRange()` として `transform.h` に追加
  - float を使用せず純粋な整数演算で実装
  - 923ケースのテストで DDA ループとの一致を確認

- **テスト**: `test/calc_valid_range_test.cpp`
  - 係数ゼロ、正/負係数、小数部base、回転シナリオ等を網羅

### 変更

- **DDA範囲チェックの最適化**
  - リリースビルド: 範囲チェック省略（calcValidRange が正確なため）
  - デバッグビルド: assert で範囲外検出時に停止

- **tx/ty 計算の修正**: `>> 8` → `>> INT_FIXED8_SHIFT`
  - 固定小数点精度を変更しても正しく動作するよう修正

---

## [2.9.0] - 2026-01-10

### 追加

- **AffineNode**: TransformNode を置き換える新しいアフィン変換ノード
  - `prepare()` で逆行列を事前計算
  - `process()` に変換処理を分離（責務分離）
  - `computeInputRequest()`: 入力要求計算を virtual 化
  - tx/ty を Q24.8 固定小数点で保持（サブピクセル精度）

- **サブピクセル精度の平行移動**: 回転・拡縮時に tx/ty の小数成分が DDA に反映
  - 1/256 ピクセル精度で平行移動を表現
  - 微調整時に滑らかなピクセルシフトを実現

- **構想ドキュメント**: `docs/ideas/IDEA_PUSH_MODE_AFFINE.md`
  - MCU 単位プッシュ処理に対応するアフィン変換の設計

### 変更

- **NodeType::Transform → NodeType::Affine**: 命名の一貫性向上
- **NODE_TYPES.transform → NODE_TYPES.affine**: JavaScript 側も同様に変更

### 非推奨（次バージョンで削除予定）

- **TransformNode** (`transform_node.h`): AffineNode に置き換え
- **transform::affine()**: AffineNode::applyAffine() に置き換え

---

## [2.8.0] - 2026-01-10

### 追加

- **循環参照検出**: ノードグラフの循環を検出しスタックオーバーフローを防止
  - `PrepareStatus` enum: `Idle`, `Preparing`, `Prepared`, `CycleError` の4状態
  - `PrepareStatus` enum: `Success=0`, `CycleDetected=1`, `NoUpstream=2`, `NoDownstream=3`
  - `pullPrepare()` / `pushPrepare()`: 循環検出時に `false` を返却
  - `pullProcess()` / `pushProcess()`: 循環エラー状態のノードは処理をスキップ
  - DAG（有向非巡回グラフ）共有ノードを正しくサポート

- **WebUI エラー通知**: 循環参照検出時にアラートを表示

### 変更

- **RendererNode::exec()**: 戻り値を `void` → `PrepareStatus` に変更
- **bindings.cpp**: `evaluateGraph()` が `int` を返却（0=成功、非0=エラー）

### テスト

- `integration_test.cpp`: 循環参照検出テストを追加
  - `pullPrepare()` での循環検出
  - `RendererNode::exec()` での循環検出

### 削除

- `docs/ideas/IDEA_CYCLE_DETECTION.md`: 実装完了により削除

---

## [2.7.0] - 2026-01-10

### 追加

- **Renderer 下流の動的接続**
  - Renderer → Filter → Sink のようなチェーン構成が可能に
  - `buildDownstreamChain()`: Renderer下流のノードチェーンを再帰構築
  - `outputConnections`: fromNodeId → toNodeId[] マップを追加

### 変更

- **Renderer → Sink 自動接続の条件緩和**
  - Sink への入力がない場合のみデフォルト接続を作成
  - Renderer下流にフィルタを配置した場合は自動接続しない

### 既知の制限

- タイル分割時、Renderer下流のBoxBlurフィルタはタイル境界で正しく動作しない
  - 上流側に配置すれば正常動作

---

## [2.6.0] - 2026-01-10

### 追加

- **WebUI: Renderer/Sink ノードの可視化**
  - ノードグラフに Renderer と Sink をシステムノードとして表示
  - 削除不可制約（システムノード保護）
  - Renderer → Sink の自動接続

- **ノード詳細パネル**
  - Renderer: 仮想スクリーンサイズ、原点、タイル分割設定
  - Sink: 出力サイズ、原点設定

### 変更

- **NodeType 再定義**: パフォーマンス計測の粒度向上
  - 新順序: Renderer(0), Source(1), Sink(2), Transform(3), Composite(4), Brightness(5), Grayscale(6), BoxBlur(7), Alpha(8)
  - C++ (`perf_metrics.h`) と JavaScript (`NODE_TYPES`) で同期

- **WebUI サイドバー簡略化**
  - 「出力設定」を削除、設定は Renderer/Sink ノード詳細パネルに移動
  - 「表示設定」として表示倍率と状態管理のみ残存

- **状態マイグレーション**
  - 旧 'output' ノード → 'sink' への自動変換
  - 旧形式の接続を Renderer 経由に自動再配線

### 修正

- bindings.cpp: renderer ノードの入力から upstream を探すように修正
- 初期化時・詳細パネル適用時に `setDstOrigin()` を呼び出し
- タイル設定変更時に `applyTileSettings()` を呼び出し
- 状態復元時にタイル設定を C++ 側に反映

---

## [2.5.1] - 2026-01-10

### 追加

- **テストパターン画像の拡充**
  - CrossHair (101×63): 奇数×奇数、180度点対称、青枠
  - SmallCheck (70×35): 偶数×奇数、5×5セルのチェック柄

### 変更

- **Checker パターン**: 128×128 → 128×96 に変更（4:3比率）

- **アフィン変換スライダーの精度向上**
  - X/Y移動: step 1 → 0.1
  - 回転: step 1 → 0.1
  - X/Y倍率: step 0.1 → 0.01
  - 行列モード a/b/c/d: step 0.1 → 0.01
  - 行列モード tx/ty: step 1 → 0.1

---

## [2.5.0] - 2026-01-10

### 追加

- **固定小数点型 (types.h)**
  - `int_fixed8` (Q24.8): 座標・origin用の固定小数点型
  - `int_fixed16` (Q16.16): 将来のアフィン行列用固定小数点型
  - 変換関数: `to_fixed8()`, `from_fixed8()`, `float_to_fixed8()` など

### 変更

- **Point 構造体**: float メンバから int_fixed8 メンバに変更
  - マイグレーション用 float コンストラクタを維持
  - `xf()`, `yf()` アクセサで float 値を取得可能

- **RenderRequest/RenderResponse**: width/height を int16_t に変更

- **ViewPort/ImageBuffer**:
  - width/height を int16_t に変更
  - stride を int32_t に変更（Y軸反転対応）

- **各ノードの origin 処理を固定小数点化**
  - SourceNode: `setOriginf()` マイグレーション API 追加
  - SinkNode: `setOriginf()` マイグレーション API 追加
  - RendererNode: `setVirtualScreenf()` マイグレーション API 追加
  - blend::first/onto: int_fixed8 引数に変更
  - transform::affine: int_fixed8 引数に変更

### 技術的詳細

- 組み込み環境への移植を見据え、浮動小数点を排除
- Q24.8 形式: 整数部24bit、小数部8bit（精度 1/256 ピクセル）
- クロスプラットフォーム対応のため明示的な型幅を使用

### ファイル追加

- `src/fleximg/types.h`: 固定小数点型定義

---

## [2.4.0] - 2026-01-10

### 変更

- **origin 座標系の統一**: RenderRequest と RenderResponse の origin を統一
  - 両方とも「バッファ内での基準点位置」を意味するように変更
  - RenderRequest: `originX/Y` → `Point2f origin` に変更
  - RenderResponse: origin の意味を反転（旧: 基準相対座標 → 新: バッファ内位置）
  - 符号反転 (`-origin.x`) が不要になり、コードが明確化

- **影響を受けるノード**:
  - SourceNode: origin 計算を新座標系に変更
  - CompositeNode: blend 引数の符号反転を削除
  - FilterNodeBase/BoxBlurNode: マージン切り出し計算を修正
  - TransformNode: affine 引数の符号反転を削除
  - SinkNode: 配置計算を新座標系に変更
  - RendererNode: RenderRequest 生成を修正

### ドキュメント

- ARCHITECTURE.md: 座標系の説明を更新
- DESIGN_TYPE_STRUCTURE.md: origin の意味を更新

---

## [2.3.1] - 2026-01-10

### ドキュメント

- **DESIGN_TYPE_STRUCTURE.md**: 型構造設計ドキュメントを新規作成
  - ViewPort、ImageBuffer、RenderResponse の設計と使用方法
  - コンポジション設計の利点を説明

- **DESIGN_PIXEL_FORMAT.md**: ピクセルフォーマット変換ドキュメントを追加
  - RGBA8_Straight ↔ RGBA16_Premultiplied 変換アルゴリズム
  - アルファ閾値定数の説明

- **GITHUB_PAGES_SETUP.md**: GitHub Pages セットアップガイドを追加
  - 自動ビルド＆デプロイの設定手順
  - トラブルシューティング

- **ドキュメント整合性修正**
  - ARCHITECTURE.md: 関連ドキュメントリンクを更新
  - DESIGN_PERF_METRICS.md: NodeType enum を8種類に更新
  - DESIGN_RENDERER_NODE.md: フィルタノード表記を修正
  - README.md: ファイル構成、ポート番号を更新
  - test/integration_test.cpp: 削除された filter_node.h の参照を修正

---

## [2.3.0] - 2026-01-10

### 追加

- **ImageBuffer::toFormat()**: 効率的なピクセルフォーマット変換メソッド
  - 右辺値参照版（`&&` 修飾）で無駄なコピーを回避
  - 同じフォーマットならムーブ、異なるなら変換

### 変更

- **FilterNode を種類別ノードクラスに分離**
  - `FilterNodeBase`: フィルタ共通基底クラス
  - `BrightnessNode`: 明るさ調整
  - `GrayscaleNode`: グレースケール変換
  - `BoxBlurNode`: ボックスブラー
  - `AlphaNode`: アルファ調整

- **NodeType の個別化**: フィルタ種類別のメトリクス計測が可能に
  - 旧: Source, Filter, Transform, Composite, Output (5種)
  - 新: Source, Transform, Composite, Output, Brightness, Grayscale, BoxBlur, Alpha (8種)

- **WebUI デバッグセクションの動的生成**
  - `NODE_TYPES` 定義による一元管理（将来のノード追加に対応）
  - `NodeTypeHelper` ヘルパー関数
  - フィルタ種類別のメトリクス表示に対応

### 削除

- **旧 FilterNode クラス**: `filter_node.h` を削除
- **FilterType enum**: 種類別ノードに置き換え

### ファイル変更

- `src/fleximg/image_buffer.h`: `toFormat()` メソッド追加
- `src/fleximg/perf_metrics.h`: NodeType enum 拡張
- `src/fleximg/nodes/filter_node_base.h`: 新規追加
- `src/fleximg/nodes/brightness_node.h`: 新規追加
- `src/fleximg/nodes/grayscale_node.h`: 新規追加
- `src/fleximg/nodes/box_blur_node.h`: 新規追加
- `src/fleximg/nodes/alpha_node.h`: 新規追加
- `src/fleximg/nodes/filter_node.h`: 削除

---

## [2.2.0] - 2026-01-10

### 変更

- **RendererNode 導入**: パイプライン実行の発火点をノードとして再設計
  - Renderer クラスを RendererNode に置き換え
  - ノードグラフの一部として統合（`src >> renderer >> sink`）
  - プル/プッシュ型の統一 API（`pullProcess()` / `pushProcess()`）

- **Node API の刷新**
  - `process()`: 共通処理（派生クラスでオーバーライド）
  - `pullProcess()` / `pushProcess()`: プル型/プッシュ型インターフェース
  - `pullPrepare()` / `pushPrepare()`: 準備フェーズの伝播
  - `pullFinalize()` / `pushFinalize()`: 終了フェーズの伝播

### 削除

- **旧 Renderer クラス**: `renderer.h`, `renderer.cpp` を削除
- **旧 API**: `evaluate()`, `UpstreamEvaluator`, `RenderContext` を削除

### ファイル変更

- `src/fleximg/nodes/renderer_node.h`: 新規追加
- `src/fleximg/node.h`: プル/プッシュ API を追加、旧 API を削除
- `src/fleximg/render_types.h`: `RenderContext` を削除

---

## [2.1.0] - 2026-01-09

### 追加

- **パフォーマンス計測基盤**: デバッグビルド時にノード別の処理時間を計測
  - `./build.sh --debug` で有効化
  - ノードタイプ別の処理時間（μs）、呼び出し回数を記録
  - Filter/Transformノードのピクセル効率（wasteRatio）を計測
  - `Renderer::getPerfMetrics()` でC++から取得
  - `evaluator.getPerfMetrics()` でJSから取得

- **メモリ確保統計**: ノード別・グローバルのメモリ使用量を計測
  - `NodeMetrics.allocatedBytes`: ノード別確保バイト数
  - `NodeMetrics.allocCount`: ノード別確保回数
  - `NodeMetrics.maxAllocBytes/Width/Height`: 一回の最大確保サイズ
  - `PerfMetrics.totalAllocatedBytes`: 累計確保バイト数
  - `PerfMetrics.peakMemoryBytes`: ピークメモリ使用量
  - `PerfMetrics.maxAllocBytes/Width/Height`: グローバル最大確保サイズ

- **シングルトンパターン**: `PerfMetrics::instance()` でグローバルアクセス
  - ImageBuffer のコンストラクタ/デストラクタで自動記録
  - 正確なピークメモリ追跡が可能に

### 改善

- **ノード詳細ポップアップ**: UI操作性の向上
  - ヘッダー部分をドラッグしてウィンドウを移動可能に
  - パネル内ボタンクリック時にポップアップが閉じる問題を修正

- **サイドバー状態の保持**: ページ再読み込み時に前回の状態を復元
  - サイドバーの開閉状態を localStorage に保存
  - アコーディオンの展開状態を localStorage に保存

### ファイル追加

- `src/fleximg/perf_metrics.h`: メトリクス構造体定義（シングルトン）
- `docs/DESIGN_PERF_METRICS.md`: 設計ドキュメント

---

## [2.0.0] - 2026-01-09

C++コアライブラリを大幅に刷新しました。

### 主な変更点

- **Node/Port モデル**: ノード間の接続をオブジェクト参照で直接管理
- **Renderer クラス**: パイプライン実行を集中管理
- **ViewPort/ImageBuffer 分離**: メモリ所有と参照の明確な責務分離
- **タイルベースレンダリング**: メモリ効率の良いタイル分割処理

### 機能

- SourceNode: 画像データの提供
- SinkNode: 出力先の管理
- TransformNode: アフィン変換（平行移動、回転、スケール）
- FilterNode: フィルタ処理（明るさ、グレースケール、ぼかし、アルファ）
- CompositeNode: 複数入力の合成

### タイル分割

- 矩形タイル分割
- スキャンライン分割
- デバッグ用チェッカーボードスキップ

### ピクセルフォーマット

- RGBA8_Straight: 入出力用
- RGBA16_Premultiplied: 内部処理用

---

旧バージョン（1.x系）の変更履歴は `fleximg_old/CHANGELOG.md` を参照してください。
