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

### 🚨 2パス評価システム（アーキテクチャ刷新）

**課題**: ~~現在のプッシュ型パイプラインでは、アフィン変換の出力ViewPortサイズがcanvasSizeに固定されており、入力画像がcanvasSizeより大きい場合に画像が切れる問題がある。~~ → **解決済み**

**解決策**: 出力ノードから上流に「必要領域」を伝播し、各ノードが必要最小限の領域のみを処理する2パス評価システムを導入する。

**詳細設計**: [DESIGN_2PASS_EVALUATION.md](DESIGN_2PASS_EVALUATION.md)

#### Phase 1: 基盤整備 ✅
- [x] `RenderContext` 構造体の定義（出力全体サイズ + タイル戦略）
- [x] `RenderRequest` 構造体の定義（部分矩形 + 基準座標）
- [x] 事前計算データ構造（AffinePreparedData, FilterPreparedData）

#### Phase 2: 事前準備（段階0）✅
- [x] `prepare()` / `prepareNode()` の実装
- [x] アフィンノード: 逆行列計算、固定小数点変換
- [x] フィルタノード: カーネル準備
- [x] 出力全体サイズの上流伝播

#### Phase 3: 要求伝播（段階1）✅
- [x] `propagateRequests()` の実装（タイルごとに呼び出し可能）
- [x] 各ノードタイプの `propagateNodeRequest()` 実装
- [x] 事前計算データを活用した高速領域変換

#### Phase 4: 評価パイプライン（段階2）✅
- [x] `evaluateTile()` / `evaluateNodeWithRequest()` の実装
- [x] ViewPortサイズを要求領域に基づいて動的決定
- [x] `applyTransform()` の改修（出力サイズパラメータ追加）

#### Phase 5: タイル分割対応 ✅
- [x] `evaluateGraph()` のタイルループ対応
- [x] タイルごとのメモリ解放
- [x] 分割戦略の選択API（None / Scanline / Tile64 / Custom）

#### Phase 6: テスト・最適化 ✅
- [x] タイル分割モードの実動作テスト
- [x] タイル境界でのピクセル整合性テスト（完全一致確認済み）
- [x] タイル分割設定UIの実装（サイドバーに追加）
- [ ] メモリ使用量の検証（将来）
- [ ] 組込み環境での動作確認（将来）

---

### ピクセルフォーマット最適化 ✅

**方針**: 入出力がRGBA8_Straightのため、Premultiplied処理が不要な場面ではRGBA8_Straightを基本とする

- [x] PixelFormatRegistryの中間形式をRGBA16_Straight → RGBA8_Straightに変更
- [x] RGBA16_Straightを廃止
- [x] `ImageProcessor::convertPixelFormat()` を廃止し `ViewPort::convertTo()` に統一

### 処理の両フォーマット対応

**方針**: アフィン変換とアルファフィルタは、RGBA8_StraightとRGBA16_Premultipliedの両方に対応

- [ ] アフィン変換: 入力フォーマットに応じた処理分岐（8bit/16bit）
- [x] アルファフィルタ: 入力フォーマットに応じた処理分岐（8bit/16bit）

### ImageProcessor撤去（長期目標）

- [ ] 残存機能をNodeGraphEvaluatorまたはフィルタに移行
- [ ] ImageProcessorクラス削除

---

## 📌 注意事項

- 新機能追加前に、`main` ブランチから新しいブランチを作成
- 実装後は CHANGELOG.md の更新を忘れずに
- 各タスクは小さなコミットに分割してプッシュ

---

## 🔗 関連ドキュメント

- [PROJECT_STATUS.md](PROJECT_STATUS.md): 現在の状態
- [CHANGELOG.md](CHANGELOG.md): 変更履歴
