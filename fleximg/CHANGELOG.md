# Changelog

このプロジェクトの主要な変更を記録します。

---

## 2026-01-08

### 合成ノードからアルファ調整機能を撤去

**背景**
- 合成ノードに組み込まれていたアルファ調整機能は、責務分離の観点から問題があった
- 処理効率面でもメリットがなかった（合成処理とは別ループで適用されていた）
- 既存のAlphaフィルタノードで同等の機能を実現可能

**変更内容**

C++ (バックエンド):
- `node_graph.h`: `CompositeInput::alpha` フィールドを削除
- `evaluation_node.h`: `CompositeEvalNode::alphas` メンバを削除
- `evaluation_node.cpp`: アルファ適用ループとalphas設定コードを削除
- `bindings.cpp`: `input.alpha` の読み取りを削除

JavaScript (フロントエンド):
- `app.js`: 合成ノードからalphaフィールドとUIを削除
- 詳細パネルにAlphaフィルタノードの使用を促すヒントを追加

**移行方法**
アルファ調整が必要な場合は、合成ノードの入力前にAlphaフィルタノードを挿入してください。

---

### 性能分析機能の修正（デバッグビルド対応）

**背景**
- 過去のリファクタリングにより `filterTime` と `affineTime` が計測されていなかった
- 処理が `NodeGraphEvaluator` から `EvaluationNode` に移動したが、計測コードが更新されていなかった

**設計方針**
- `FLEXIMG_DEBUG` マクロで計測機能を制御（リリースビルドでは完全除去）
- `RenderContext` 経由で `PerfMetrics*` を共有（シグネチャ変更なし）
- `PerfMetrics` を配列形式に変更し、拡張性と保守性を向上
- 計測時間は `uint32_t` マイクロ秒（整数演算のみ、FPU不要）

**変更内容**
- `node_graph.h`: マクロ定義、`PerfMetricIndex` namespace、`PerfMetrics` 配列化
- `node_graph.cpp`: 計測コードをifdef囲み
- `evaluation_node.cpp`: Filter/Affineの計測コード追加
- `bindings.cpp`: `getPerfMetrics()` を配列形式対応
- `build.sh`: `--debug` オプション追加
- `Makefile`: `FLEXIMG_DEBUG=1` オプション追加

**デバッグステータスバー**
- 画面最下部に計測結果を常時表示
- サイドバーと連動して左位置が調整される
- ターミナル風デザイン（黒背景 + 緑文字）

**使用方法**
```bash
# WASMデバッグビルド
./build.sh --debug

# ネイティブデバッグビルド
make test FLEXIMG_DEBUG=1
```

---

### NewViewPort → ViewPort リネーム

移行期間用の仮名称 `NewViewPort` を正式名称 `ViewPort` にリネーム。

**変更内容**
- `viewport_new.h/cpp` → `viewport.h/cpp` にファイル名変更
- `struct NewViewPort` → `struct ViewPort` にクラス名変更
- 全ソースファイルでの参照を更新
- 60テスト全てパス

---

### ViewPort構造リファクタリング

**設計変更**
- 旧 `ViewPort` を責務分離し、3つの型に再構築:
  - `ImageBuffer`: メモリ所有 (RAII)、確保・解放を管理
  - `NewViewPort`: 純粋ビュー（軽量、所有権なし）
  - `EvalResult`: パイプライン評価結果（ImageBuffer + origin座標）

**主要な変更ファイル**
- `image_buffer.h/cpp`: 新規作成（メモリ所有画像）
- `viewport_new.h/cpp`: 新規作成（純粋ビュー + ブレンド操作）
- `eval_result.h`: 新規作成（評価結果構造体）
- `operators.h/cpp`: `OperatorInput`/`EvalResult` APIに移行
- `evaluation_node.cpp`: 一時的ViewPort変換を削除
- `viewport.h/cpp`: 削除（旧ViewPort廃止）

**オペレーター変更**
- `NodeOperator::apply()`: 戻り値を `EvalResult` に変更
- `SingleInputOperator::applyToSingle()`: 引数を `OperatorInput` に変更
- `AffineOperator`: コンストラクタ簡素化（7引数 → 5引数）
- `CompositeOperator`: 静的メソッド化（`createCanvas`, `blendFirst`, `blendOnto`）

**テスト更新**
- `viewport_test.cpp`: `ImageBuffer`/`NewViewPort` テストに完全書き換え (24テスト)
- `affine_mapping_test.cpp`: 新API対応 (36テスト)
- 合計60テストがパス

**設計ドキュメント**
- `docs/DESIGN_VIEWPORT_REFACTOR.md` 完了

---

### アルファ変換最適化

**新方式の導入: `A_tmp = A8 + 1`**
- Forward変換（8bit→16bit）: 除算ゼロ、乗算のみで高速化
- Reverse変換（16bit→8bit）: 除数が1-256に限定（テーブル化やSIMD最適化が容易）
- 透明ピクセル（A8=0）でもRGB情報を保持（将来の「アルファを濃くするフィルタ」に対応）

**アルファ閾値の定義**
- `ALPHA_TRANSPARENT_MAX = 255`: この値以下は透明
- `ALPHA_OPAQUE_MIN = 65280`: この値以上は不透明
- `PixelFormatIDs::RGBA16Premul` 名前空間に constexpr 定数として定義

**変更ファイル**
- `pixel_format.h`: RGBA16Premul 名前空間と閾値定数を追加
- `pixel_format_registry.cpp`: 変換関数を新方式に更新
- `operators.cpp`: 合成処理の閾値判定を更新

**テスト追加**
- `AlphaConversion_TransparentPreservesRGB`: 透明時RGB保持の検証
- `AlphaConversion_Roundtrip`: 往復変換の値保持検証
- `AlphaConversion_ThresholdConstants`: 閾値定数の検証

**設計ドキュメント**
- `docs/DESIGN_ALPHA_CONVERSION.md` を新規作成

---

### コード共通化リファクタリング

**タイル評価ループの統合**
- `evaluateWithPipeline`の「タイル分割なし」と「タイル分割あり」を統合
- `TileStrategy::None`は1x1タイルとして処理
- パフォーマンス計測を全モードで有効化

**CompositeOperator::apply()の簡素化**
- `node_graph.cpp`: `compositeOp->apply()`を`createCanvas+blendFirst`に置換
- `operators.cpp`: `apply()`を`createCanvas+blendOnto`で再実装
- 約60行のブレンドロジック重複を解消

**削減効果**: 約100行

---

### 部分ViewPort返却によるメモリ最適化

**設計原則**
- 要求された範囲のうち、実際に保有しているデータ量だけ返す
- 空タイル（画像と重ならない）はメモリ確保・処理をスキップ

**変更内容**
- `ImageEvalNode`: 交差なし時に空ViewPort（width=0）を返却
- `FilterEvalNode`: 空入力時に早期リターン
- `AffineEvalNode`: 空入力時に早期リターン
- `CompositeEvalNode`: 空入力をスキップ
- `render()`: 空タイルのコピーをスキップ

**期待される効果**
- 「小さい画像 + 大きいキャンバス + タイル分割」で大幅なメモリ節約
- 空タイルでのオペレータ処理が完全にスキップ

---

### 逐次合成方式によるメモリ効率改善

**設計変更**
- 「全入力を収集→一括合成」から「入力を1つずつ取得→逐次合成」に変更
- メモリ使用量: O(n) → O(2) （canvas + 現在の入力1つ）

**実装詳細**
- `CompositeOperator`: 逐次合成用メソッド追加
  - `createCanvas()`: 透明キャンバス作成
  - `blendFirst()`: 最初の入力用（memcpy最適化）
  - `blendOnto()`: 2枚目以降用（ブレンド処理）
  - `coversFullRequest()`: 完全カバー判定
- `CompositeEvalNode`: 逐次合成ループに変更
  - 完全カバー入力はmoveでキャンバスに（メモリ確保なし）
  - 部分オーバーラップ入力はキャンバス確保+memcpy
  - 2枚目以降はブレンド処理

**最適化ポイント**
- 最初の入力が透明キャンバスへの合成 → memcpyで十分（ブレンド計算不要）
- 各入力はループ終了時に即座に解放

---

### double → float への変更（組込みシステム最適化）

**背景**
- 組込みシステム（ESP32等）では float が高速、double は遅い
- 精度より速度を優先する設計方針

**変更ファイル**
- `image_types.h`: AffineMatrix のメンバを float に変更
- `viewport.h/cpp`: srcOriginX/Y を float に変更
- `node_graph.h/cpp`: RenderRequest, RenderContext, PerfMetrics 等を float に変更
- `operators.h/cpp`: AffineOperator のパラメータを float に変更
- `evaluation_node.h/cpp`: 各種座標計算を float に変更
- `test/*.cpp`: EXPECT_DOUBLE_EQ → EXPECT_FLOAT_EQ に変更、リテラルを float 化

---

### AffineOperator テストの設計修正

**設計仕様の明確化**
- 「基準点アライメント」設計: 入力画像の基準点が出力バッファの基準点に揃う
- `inputSrcOrigin`: 基準点から見た入力画像左上の相対座標
  - 左上原点: (0, 0)
  - 中央原点: (-2, -3) ← 基準点から左上へ (-2, -3)
  - 右下原点: (-4, -6)
- `outputOffset = dstOrigin - inputSrcOrigin` で出力位置を計算

**テスト修正内容**
- ExpectedRange 構造体を新設計に合わせて更新
- AffineOperator コンストラクタの呼び出しを evaluation_node.cpp と同じパターンに統一
- 期待値を基準点アライメント設計に基づき再計算
- 全36テストがパス

---

## 2026-01-07

### ノード配置UXの改善

**表示範囲中央への配置**
- ノード追加時、スクロール位置に関係なく現在の表示範囲の中央付近に配置
- 画像ノードは中央より左寄りに配置（パイプライン構造を意識した配置）
- ±16pxのランダムオフセットで自然な配置感を演出

**既存ノードの自動押し出し**
- 新規ノード追加時、重なる既存ノードを自動的に押し出す
- 押し出し量は控えめ（50px）で、多少の重なりは許容
- 300msのease-outアニメーションでスムーズに移動

**Chrome Scroll Anchoring問題の修正**
- リロード後にノードをドラッグするとスクロールが追従する問題を修正
- 原因: ChromeのScroll Anchoring機能が特定の要素をアンカーとして記憶
- 修正: `overflow-anchor: none` で無効化

---

### バグ修正

**合成→アフィン変換の画像切れ問題を修正**
- 合成ノードを経由してからアフィン変換ノードに接続すると、画像の左上部分が表示されなくなる問題を修正
- 原因: `AffineEvalNode::computeInputRequest` が入力要求の `originX/originY` を `0, 0` に設定していたため、合成ノードが誤った基準点で画像を配置していた
- 修正: `outputRequest.originX/originY` を保持して上流ノードに伝播するように変更

---

### ノードグラフUIの改善（ハイブリッド詳細パネル）

**コンパクト表示への移行**
- 各ノードタイプを簡略化し、主要パラメータのみノード上に表示
  - 画像ノード: サムネイル + 原点表示（テキスト）
  - フィルタノード: 最初のパラメータのみ
  - 合成ノード: 入力数表示のみ
  - アフィンノード: 回転パラメータのみ
- ノード高さを大幅に削減（情報過多の解消）

**詳細パネルの追加**
- ダブルクリックまたは右クリックメニュー「詳細」で開くフローティングパネル
- 全パラメータを詳細パネルで編集可能
- 合成ノード: 入力追加ボタンもパネル内に配置
- アフィンノード: パラメータ/行列モード切替もパネル内に配置

**合成ノードの可変高さ**
- 入力数に応じてノード高さを自動調整
- ポート間隔を最低15px確保し、クリックしやすさを向上

---

### スクロール管理の共通化

**スクロールマネージャーの導入**
- `createScrollManager()` 関数で比率ベースのスクロール管理を共通化
- プレビューエリアとノードグラフエリアの両方で使用

**プレビューエリアの改善**
- 小さい出力画像を中央に配置（CSS Grid + margin: auto）
- リサイズ時にスクロール位置の比率を維持

---

### fleximg への名称変更とArduino互換構造へのリファクタリング

**プロジェクト構造の変更**
- `image-transform-preview` → `fleximg` に名称変更
- コアライブラリを `src/fleximg/` に配置（Arduino互換構造）
- Webデモを `demo/web/` に分離
- バインディングコードを `demo/bindings.cpp` に移動

**名前空間の統一**
- `namespace ImageTransform` → `namespace FLEXIMG_NAMESPACE` に変更
- マクロによるカスタマイズ対応（デフォルト: `fleximg`）

**ビルドシステムの更新**
- `build.sh`: 新しいパス構造に対応、`-I src` フラグ追加
- `Makefile`: src/fleximg/ パスに更新
- `.github/workflows/deploy.yml`: fleximg/demo/web/ からデプロイ

**ドキュメント更新**
- README.md: タイトル、構造説明、パスを更新
- CLAUDE.md, CLAUDE.local.md: ビルドパスを更新
- docs/: QUICKSTART.md, PROJECT_STATUS.md, GITHUB_PAGES_SETUP.md を更新

---

### ネイティブテスト環境の構築

**テストフレームワーク導入**
- Google Test (gtest 1.17.0) を Homebrew 経由で導入
- `make test` でテストランナーを実行可能に
- パラメータ化テスト対応

**ViewPort 単体テスト (21テスト)**
- 構築・メモリレイアウト・ピクセルアクセス
- コピー/ムーブセマンティクス
- SubView 作成・アクセス
- フォーマット変換 (RGBA8 ↔ RGBA16)
- Image との相互変換
- srcOrigin の保持

**AffineOperator テスト (39テスト)**
- 12パターンの出力範囲検証（3原点 × 4角度）
- ピクセルマッピングの正確性検証
- ±1度安定性テスト
- 単位行列・平行移動の基本テスト

**CLIツール**
- `build/imgproc` - コマンドライン画像処理ツール
- オプション: `--brightness`, `--grayscale`, `--alpha`
- stb_image/stb_image_write による画像I/O

### AffineOperator テスト修正

**設計仕様の明確化**
- `originX/Y` パラメータは「回転中心点」として実装されている
- 単位行列（0度回転）の場合、origin は効果を持たない（数学的に正しい動作）
- テスト期待値を回転中心設計に基づき再計算

**修正内容**
- 12パターンの期待値を回転中心設計に合わせて修正
- `verifyPixelMapping` 関数の計算式を修正
- テストコメントに回転中心設計の数式を明記

### 解決済みの問題

**Premultiply 精度問題（解決）**
- `RGBA8_Straight` → `RGBA16_Premultiplied` 変換での精度損失を修正
- 旧方式: `(r16 * a16) >> 16` による丸め誤差
- 新方式: `A_tmp = A8 + 1` による除算回避アルゴリズムで根本解決

---

## 2026-01-06

### ピクセルフォーマット最適化

**標準フォーマットの変更**
- 標準（中間）フォーマットを `RGBA16_Straight` → `RGBA8_Straight` に変更
- `RGBA16_Straight` フォーマットを完全に廃止
- 変換時の中間バッファが 16bit → 8bit に削減（メモリ効率向上）

**API統一**
- `ImageProcessor::convertPixelFormat()` を廃止
- フォーマット変換は `ViewPort::convertTo()` に統一
- node_graph.cpp 内の7箇所の変換呼び出しを更新

**アルファフィルタの両フォーマット対応**
- 入力が `RGBA16_Premultiplied` → 16bit処理（RGBA全チャンネル乗算）
- 入力が `RGBA8_Straight` → 8bit処理（Alphaのみ乗算、変換不要）
- その他 → `RGBA8_Straight` に変換して8bit処理

---

### タイル分割出力機能（Phase 6）

**タイル分割設定UI**
- サイドバーにタイル分割戦略セレクトボックスを追加
- 4つの分割戦略を選択可能:
  - なし（一括処理）: 従来互換モード
  - スキャンライン（1行ずつ）: 極小メモリ環境向け
  - 64x64タイル: 組込み環境標準
  - カスタム: 任意サイズのタイル
- カスタムモード時は幅・高さ入力欄を表示

**WASMバインディング拡張**
- `setTileStrategy(strategy, tileWidth, tileHeight)` APIを追加
- JavaScript側からタイル分割戦略を設定可能に

**ピクセル整合性テスト**
- 「なし」モードと「64x64タイル」モード: 完全一致
- 「なし」モードと「スキャンライン」モード: 完全一致
- タイル境界での継ぎ目問題なし

---

### 2パス評価システム（タイル分割対応）

**画像切れ問題の修正**
- キャンバスサイズ < 入力画像サイズ + オフセット の場合に発生していた画像切れを修正
- `applyTransform()` / `mergeImages()` に出力サイズパラメータを追加
- アフィン変換の出力サイズを入力画像サイズに基づいて動的決定

**3段階評価アーキテクチャ**
- 段階0（prepare）: 逆行列計算、フィルタカーネル準備などの事前計算
- 段階1（propagateRequests）: タイルごとの必要領域を上流に伝播
- 段階2（evaluateTile）: 要求領域のみを処理

**新しいデータ構造**
- `TileStrategy`: 分割戦略（None / Scanline / Tile64 / Custom）
- `RenderContext`: 出力全体情報 + タイル設定
- `RenderRequest`: 部分矩形要求
- `AffinePreparedData` / `FilterPreparedData`: 事前計算データ

**将来の拡張基盤**
- 組込み環境向けタイル分割処理の基盤を整備
- メモリ使用量を制限しながら大きな画像を処理可能に

---

## 2026-01-05

### 状態保存/復元機能

**LocalStorage自動保存**
- ノード追加・削除、接続変更、パラメータ変更時に自動保存
- ページリロード時に前回の状態を自動復元
- 画像ライブラリ（画像データ含む）も保存対象

**URLパラメータ対応**
- 「URLコピー」ボタンで現在の状態をURLにエンコード
- URLを共有することでノードグラフ構成を共有可能
- 同一ブラウザではLocalStorageから画像データを補完

**リセット機能**
- 「リセット」ボタンで保存された状態をクリア
- 初期状態（テストパターン画像）に戻る

### アーキテクチャ改善

**フィルタレジストリパターンの導入**
- JS側: `FILTER_DEFINITIONS`オブジェクトでフィルタ定義を一元管理
  - 新規フィルタ追加時は定義オブジェクトに追加するだけでOK
  - セレクトオプション、UI、デフォルト値が自動生成
- C++側: `FilterRegistry`クラスでフィルタを一元管理
  - if-elseチェーンを削減し、ファクトリパターンに移行
  - 複数パラメータ対応（`filterParams`配列）

**対応フィルタ**
- グレースケール、明るさ、ぼかし、アルファ

### 開発者向け機能

**テストパターン画像の自動生成**
- アプリ起動時に3種類のテストパターン画像を自動生成
  - Checker: チェッカーパターン（青白格子、中央に赤い点）
  - Target: 同心円ターゲット（カラフルな同心円）
  - Grid: グリッド＋十字線（対角線付き）
- アフィン変換や合成のデバッグに便利な点対称パターン

### パフォーマンス最適化

**mergeImages関数の高速化**
- `getPixelPtr`呼び出しを内側ループから外側ループに移動（O(W×H) → O(H)）
- アルファ値による早期終了/スキップ最適化
- 条件付き加算パターンで分岐を統合

**詳細パフォーマンス計測機能**
- WASM内部の処理時間を工程別に計測（Filter/Affine/Composite/Convert/Output）
- コンソールに詳細ログを出力

### UI/UX改善

**左サイドバーメニュー**
- 画像ライブラリと出力設定を統合
- トグルボタンで開閉（ESCキー、オーバーレイクリック対応）
- レスポンシブデザイン（PC/モバイル対応）

**レイアウト改善**
- ノードグラフとプレビュー間にリサイズ可能なスプリッター追加
- プレビュー倍率スライダー追加（1x〜5x）
- スクロール位置の自動中央調整
- ヘッダーのコンパクト化

### 簡素化

- 合成ノードからアフィンパラメータを削除（アフィン変換は専用ノードで）

---

## 2026-01-04

### アーキテクチャ改善

- WebAssemblyバインディングを整理し、NodeGraphEvaluator専用アーキテクチャに移行
- 未使用コード約410行を削除

### バグ修正

- 奇数幅画像での斜め歪みバグを修正

---

## 2026-01-03

### 内部リファクタリング

- 拡張可能ピクセルフォーマットシステムの実装
- ViewPort統一画像型の導入
- フィルタ処理の数学的正確性を改善（Straight alpha形式で処理）
- モジュラーファイル構造への分離

---

## 2026-01-02

### ノードグラフエディタへの移行

**新機能**
- ノードグラフエディタの実装（5種類のノード: 画像、アフィン変換、合成、フィルタ、出力）
- 画像ライブラリ機能
- C++ NodeGraphEvaluator（トポロジカルソート、メモ化）
- ドラッグ&ドロップによるノード配置・接続

**UI/UX改善**
- 動的ノード高さ調整
- リアルタイム接続線更新
- コンテキストメニュー（右クリック）

**ドキュメント整備**
- README.md、QUICKSTART.md、各種ドキュメントの作成・更新

---

*詳細な変更履歴については Git コミット履歴を参照してください。*
