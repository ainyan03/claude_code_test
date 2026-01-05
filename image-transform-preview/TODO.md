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

### ピクセルフォーマット最適化

**方針**: 入出力がRGBA8_Straightのため、Premultiplied処理が不要な場面ではRGBA8_Straightを基本とする

- [ ] PixelFormatRegistryの中間形式をRGBA16_Straight → RGBA8_Straightに変更
- [ ] RGBA8_Straight ↔ RGBA16_Premultiplied の直接変換パスをレジストリに追加
- [ ] RGBA16_Straightを廃止
- [ ] `ImageProcessor::convertPixelFormat()` を廃止し `ViewPort::convertTo()` に統一

### 処理の両フォーマット対応

**方針**: アフィン変換とアルファフィルタは、RGBA8_StraightとRGBA16_Premultipliedの両方に対応

- [ ] アフィン変換: 入力フォーマットに応じた処理分岐（8bit/16bit）
- [ ] アルファフィルタ: 入力フォーマットに応じた処理分岐（8bit/16bit）

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
