# Changelog

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
