# TODO リスト

## 🎯 機能拡張（優先度順）

### 高優先度（品質管理）

#### ブラーフィルタ境界処理
- [ ] 画像境界でブラーが効かない問題の修正
  - 現状: 元画像の範囲外にはブラーが広がらず、境界がくっきり見える
  - 原因: ImageEvalNodeが画像範囲外のピクセルを返さない
  - 対策案: 境界ピクセルの延長（clamp）またはアルファを考慮した処理

#### ユニットテスト
- [x] テストフレームワークの導入（Google Test 1.17.0） ✅ `make test`
- [x] ImageBuffer/NewViewPort の単体テスト ✅ `test/viewport_test.cpp` (24テスト)
- [x] AffineOperator の単体テスト ✅ `test/affine_mapping_test.cpp` (36テスト)
- [ ] その他 Operators の単体テスト（Brightness, Grayscale, BoxBlur, Alpha）
- [ ] NodeGraphEvaluator の統合テスト

#### 精度改善
- [x] Premultiply変換の精度改善 ✅ `A_tmp = A8 + 1` 方式で除算回避
  - 詳細設計: [docs/DESIGN_ALPHA_CONVERSION.md](docs/DESIGN_ALPHA_CONVERSION.md)

#### エラーハンドリング強化
- [ ] ノードグラフの循環依存検出とエラー表示
- [ ] 画像読み込みエラーの詳細表示
- [ ] トースト通知でのエラー表示

#### コード品質
- [ ] アフィン変換: 入力フォーマットに応じた処理分岐（8bit/16bit）
- [x] アルファフィルタ: 入力フォーマットに応じた処理分岐（8bit/16bit）

---

### 中優先度（Webインターフェース）

#### ノードグラフの保存/読み込み機能
- [x] ノードグラフをJSONシリアライズする関数 ✅ `getAppState()`
- [x] JSONからノードグラフをデシリアライズする関数 ✅ `restoreAppState()`
- [x] LocalStorageへの自動保存 ✅ 状態変更時に自動保存
- [x] URLパラメータ対応 ✅ 状態をURLにエンコード/デコード
- [ ] 「保存」ボタンの追加（JSONファイルダウンロード）
- [ ] 「読み込み」ボタンの追加（JSONファイルアップロード）

#### ノードプレビュー機能
- [ ] ノードごとの小さなサムネイル表示
- [ ] ホバー時に拡大プレビュー

#### アンドゥ/リドゥ機能
- [ ] コマンドパターンの実装
- [ ] 履歴スタックの管理
- [ ] Ctrl+Z / Ctrl+Shift+Z のキーバインド

#### モバイルUI最適化
- [ ] タッチジェスチャーの改善（ピンチズーム等）
- [ ] ノードサイズの動的調整
- [ ] モバイル専用レイアウト

---

### 低優先度

#### CLIツール拡張
- [x] ネイティブビルド環境の構築 ✅ `Makefile`
- [x] 基本的なCLIツール ✅ `cli/main.cpp`
- [ ] JSON設定ファイル対応（ノードグラフ定義）

#### マルチ出力ノード
- [ ] 複数の出力ノードを許可
- [ ] 各出力ノードに個別のキャンバス
- [ ] サイドバイサイド表示

---

## 🎨 追加予定のノード種類

将来的に追加を検討しているノードタイプの一覧です。

### フィルタ系ノード
| ノード名 | 説明 | パラメータ |
|----------|------|------------|
| ガウシアンぼかし | 滑らかなぼかし効果 | 半径 (0-50px) |
| トリミング/クロップ | 矩形領域の切り出し | X, Y, 幅, 高さ |
| 色調補正 | HSV変換による色調整 | 色相, 彩度, 明度 |
| コントラスト | コントラスト調整 | 強度 (-100〜+100) |
| シャープネス | エッジ強調 | 強度 |
| ノイズ除去 | メディアンフィルタ等 | 半径 |

### 合成系ノード
| ノード名 | 説明 | パラメータ |
|----------|------|------------|
| マスク合成 | マスク画像による合成 | マスク入力 |
| ブレンドモード | 乗算・スクリーン等 | モード選択 |

### 生成系ノード
| ノード名 | 説明 | パラメータ |
|----------|------|------------|
| グラデーション | 線形/放射グラデーション | 色, 方向 |
| パターン生成 | チェッカー, ストライプ等 | サイズ, 色 |

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

### アフィン変換の入力要求範囲分割（検討中）

**前提条件**: パフォーマンス測定基盤の整備が必要

入力AABBをY方向に分割し、不要ピクセルの取得を削減する最適化。45度回転時に約50倍の効率改善が期待できる。上流ノードタイプをビットマスクで判定し、重いフィルタがある場合のみ分割を実行。

詳細: [docs/DESIGN_AFFINE_REQUEST_SPLITTING.md](docs/DESIGN_AFFINE_REQUEST_SPLITTING.md)

---

### In-place Mutation 最適化（検討中）

**前提条件**: パフォーマンス測定基盤の整備が必要

**アイデア**: 上流ノードが「改変可能」フラグをつけてEvalResultを渡すことで、下流ノード（Brightness, Grayscale, Alpha等の単純フィルタ）が新規メモリ確保せずに直接バッファを改変できるようにする。

- [ ] パフォーマンス測定基盤の整備（メモリ確保回数、処理時間の計測）
- [ ] EvalResultに `canMutate` フラグを追加
- [ ] 対応フィルタの実装（フォーマット一致時のみ有効）
- [ ] 効果測定と評価

**期待効果**:
- メモリ確保・コピーのオーバーヘッド削減
- 連続するフィルタチェーンで効果大

**注意点**:
- フォーマットが一致する場合のみ有効
- 複雑性が増すため、効果測定で判断

---

### 完了済みリファクタリング

以下のリファクタリングは完了しています：

- **ノードオペレーター統一**: フィルタ・合成・アフィン変換を統一的なオペレータークラスで管理
- **パイプラインベース評価**: 文字列ベースのノード検索をポインタ直接参照に置き換え
- **タイル座標系**: 「基準相対座標系」に統一、タイル分割とアフィン変換の組み合わせ問題を解決
- **ViewPort構造**: 3つの型（ImageBuffer, ViewPort, EvalResult）に責務分離

詳細は [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) を参照。

---

## 📌 注意事項

- 新機能追加前に、`main` ブランチから新しいブランチを作成
- 実装後は CHANGELOG.md の更新を忘れずに
- 各タスクは小さなコミットに分割してプッシュ

---

## 🔗 関連ドキュメント

- [CHANGELOG.md](CHANGELOG.md): 変更履歴
- [docs/PROJECT_STATUS.md](docs/PROJECT_STATUS.md): 現在の状態
- [docs/QUICKSTART.md](docs/QUICKSTART.md): クイックスタートガイド

### 設計ドキュメント

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): アーキテクチャ概要
- [docs/DESIGN_TILE_COORDINATE_SYSTEM.md](docs/DESIGN_TILE_COORDINATE_SYSTEM.md): タイル座標系設計
- [docs/DESIGN_VIEWPORT_REFACTOR.md](docs/DESIGN_VIEWPORT_REFACTOR.md): 型構造設計
- [docs/DESIGN_ALPHA_CONVERSION.md](docs/DESIGN_ALPHA_CONVERSION.md): ピクセルフォーマット変換設計
