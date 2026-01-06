# TODO リスト

## 🎯 機能拡張（優先度順）

### 高優先度

#### ノードグラフの保存/読み込み機能
- [x] ノードグラフをJSONシリアライズする関数 ✅ `getAppState()`
- [x] JSONからノードグラフをデシリアライズする関数 ✅ `restoreAppState()`
- [x] LocalStorageへの自動保存 ✅ 状態変更時に自動保存
- [x] URLパラメータ対応 ✅ 状態をURLにエンコード/デコード
- [ ] 「保存」ボタンの追加（JSONファイルダウンロード）
- [ ] 「読み込み」ボタンの追加（JSONファイルアップロード）

#### ぼかしフィルタノード
- [ ] C++側: ガウシアンぼかし処理の実装
- [ ] Emscriptenバインディングの追加
- [ ] JavaScript側: ぼかしノードUIの追加
- [ ] パラメータ: ぼかし半径（0-50px）

#### トリミング/クロップノード
- [ ] C++側: 矩形トリミング処理
- [ ] パラメータ: X, Y, 幅, 高さ
- [ ] JavaScript側: トリミングノードUI

---

### 中優先度

#### 色調補正ノード
- [ ] C++側: RGB↔HSV変換
- [ ] パラメータ: 色相、彩度、明度
- [ ] JavaScript側: 色調補正ノードUI

#### ノードプレビュー機能
- [ ] ノードごとの小さなサムネイル表示
- [ ] ホバー時に拡大プレビュー

---

### 低優先度

#### ユニットテスト
- [ ] テストフレームワークの導入（Google Test等）
- [ ] NodeGraphEvaluator のテスト
- [ ] 各ノードタイプの単体テスト

#### エラーハンドリング強化
- [ ] ノードグラフの循環依存検出とエラー表示
- [ ] 画像読み込みエラーの詳細表示
- [ ] トースト通知でのエラー表示

#### モバイルUI最適化
- [ ] タッチジェスチャーの改善（ピンチズーム等）
- [ ] ノードサイズの動的調整
- [ ] モバイル専用レイアウト

#### アンドゥ/リドゥ機能
- [ ] コマンドパターンの実装
- [ ] 履歴スタックの管理
- [ ] Ctrl+Z / Ctrl+Shift+Z のキーバインド

#### マルチ出力ノード
- [ ] 複数の出力ノードを許可
- [ ] 各出力ノードに個別のキャンバス
- [ ] サイドバイサイド表示

---

## 🧪 実験的アイデア

- WebGPU対応（GPU計算による高速化）
- AIフィルタノード（TensorFlow.js等）
- コラボレーション機能（WebSocket）

---

## 📝 メンテナンスタスク

- [ ] Emscriptenのバージョン更新
- [ ] GitHub Actionsのアクション最新化
- [ ] スクリーンショットの追加
- [ ] チュートリアル動画の作成

---

## 🔄 リファクタリング計画

### ピクセルフォーマット最適化 ✅

**方針**: 入出力がRGBA8_Straightのため、Premultiplied処理が不要な場面ではRGBA8_Straightを基本とする

- [x] PixelFormatRegistryの中間形式をRGBA16_Straight → RGBA8_Straightに変更
- [x] RGBA16_Straightを廃止
- [x] `ImageProcessor::convertPixelFormat()` を廃止し `ViewPort::convertTo()` に統一

### 処理の両フォーマット対応

**方針**: アフィン変換とアルファフィルタは、RGBA8_StraightとRGBA16_Premultipliedの両方に対応

- [ ] アフィン変換: 入力フォーマットに応じた処理分岐（8bit/16bit）
- [x] アルファフィルタ: 入力フォーマットに応じた処理分岐（8bit/16bit）

### ノードオペレーター統一設計

**詳細設計**: [DESIGN_NODE_OPERATOR.md](DESIGN_NODE_OPERATOR.md)

**目標**: フィルタ・合成・アフィン変換を統一的なオペレータークラスで管理

#### Phase 1: 基盤整備 ✅
- [x] `OperatorContext` 構造体の定義
- [x] `NodeOperator` 基底クラスの定義
- [x] `SingleInputOperator` 基底クラスの定義
- [x] `operators.h` / `operators.cpp` ファイル作成

#### Phase 2: フィルタオペレーター移行 ✅
- [x] `BrightnessOperator` 実装（既存フィルタをラップ）
- [x] `GrayscaleOperator` 実装
- [x] `BoxBlurOperator` 実装
- [x] `AlphaOperator` 実装
- [x] `OperatorFactory::createFilterOperator()` 実装

#### Phase 3: アフィン・合成オペレーター実装 ✅
- [x] `AffineOperator` 実装（`ImageProcessor::applyTransform` から移植）
- [x] `CompositeOperator` 実装（`ImageProcessor::mergeImages` から移植）
- [x] `OperatorFactory` に `createAffineOperator()`, `createCompositeOperator()` 追加

#### Phase 4: NodeGraphEvaluator 統合 ✅
- [x] `evaluateNode()` をオペレーター呼び出しに変更
- [x] `evaluateNodeWithRequest()` をオペレーター呼び出しに変更
- [x] `evaluateGraph()` 内の `mergeImages` をオペレーター呼び出しに変更

#### Phase 5: クリーンアップ ✅
- [x] `ImageProcessor` クラスを完全削除（`image_processor.h/cpp`）
- [x] `FilterRegistry` クラスを完全削除（`filter_registry.h/cpp`）
- [x] `NodeGraphEvaluator` から `processor` メンバーを削除
- [x] `build.sh` を更新

#### Phase 6: フィルタ処理のオペレーター統合 ✅
- [x] `BrightnessOperator` にフィルタ処理を直接実装
- [x] `GrayscaleOperator` にフィルタ処理を直接実装
- [x] `BoxBlurOperator` にフィルタ処理を直接実装
- [x] `AlphaOperator` にフィルタ処理を直接実装
- [x] `filters.h/cpp` を削除

### パイプラインベース評価システム ✅

**詳細設計**: [DESIGN_PIPELINE_EVALUATION.md](DESIGN_PIPELINE_EVALUATION.md)

**目標**: 文字列ベースのノード検索をポインタ直接参照に置き換え、評価効率を向上

#### Phase 1: 基盤整備 ✅
- [x] `EvaluationNode` 基底クラスの定義
- [x] `evaluation_node.h` ファイル作成

#### Phase 2: 派生クラス実装 ✅
- [x] `ImageEvalNode` 実装
- [x] `FilterEvalNode` 実装
- [x] `AffineEvalNode` 実装
- [x] `CompositeEvalNode` 実装
- [x] `OutputEvalNode` 実装

#### Phase 3: PipelineBuilder実装 ✅
- [x] `Pipeline` 構造体（全ノードの所有権を保持）
- [x] `PipelineBuilder::build()` でGraphNode/ConnectionをEvaluationNodeグラフに変換
- [x] ポインタによるノード接続

#### Phase 4: NodeGraphEvaluator統合 ✅
- [x] `pipeline_` メンバーと `pipelineDirty_` フラグ追加
- [x] `buildPipelineIfNeeded()` 実装
- [x] `evaluateWithPipeline()` 実装

#### Phase 5: テスト・最適化 ✅
- [x] 動作確認（全ノードタイプ）
- [ ] パフォーマンス計測・比較（将来）

#### Phase 6: 旧実装削除 ✅
- [x] 文字列ベース評価コードを削除（約680行削減）
- [x] `usePipeline_` フラグを削除
- [x] 旧キャッシュ変数を削除（`nodeResultCache`, `affinePreparedCache`, 等）
- [x] 旧ヘルパー関数を削除（`findOutputNode`, `findInputConnection`, 等）

---

## 📌 注意事項

- 新機能追加前に、`main` ブランチから新しいブランチを作成
- 実装後は CHANGELOG.md の更新を忘れずに
- 各タスクは小さなコミットに分割してプッシュ

---

## 🔗 関連ドキュメント

- [PROJECT_STATUS.md](PROJECT_STATUS.md): 現在の状態
- [CHANGELOG.md](CHANGELOG.md): 変更履歴
