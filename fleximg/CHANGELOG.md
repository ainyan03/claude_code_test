# Changelog

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
  - `PrepareState` enum: `Idle`, `Preparing`, `Prepared`, `CycleError` の4状態
  - `ExecResult` enum: `Success=0`, `CycleDetected=1`, `NoUpstream=2`, `NoDownstream=3`
  - `pullPrepare()` / `pushPrepare()`: 循環検出時に `false` を返却
  - `pullProcess()` / `pushProcess()`: 循環エラー状態のノードは処理をスキップ
  - DAG（有向非巡回グラフ）共有ノードを正しくサポート

- **WebUI エラー通知**: 循環参照検出時にアラートを表示

### 変更

- **RendererNode::exec()**: 戻り値を `void` → `ExecResult` に変更
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

- **RenderRequest/RenderResult**: width/height を int16_t に変更

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

- **origin 座標系の統一**: RenderRequest と RenderResult の origin を統一
  - 両方とも「バッファ内での基準点位置」を意味するように変更
  - RenderRequest: `originX/Y` → `Point2f origin` に変更
  - RenderResult: origin の意味を反転（旧: 基準相対座標 → 新: バッファ内位置）
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
  - ViewPort、ImageBuffer、RenderResult の設計と使用方法
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
