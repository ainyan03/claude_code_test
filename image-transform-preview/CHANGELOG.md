# Changelog

このプロジェクトの主要な変更を記録します。

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

### 既知の技術的負債
- `src/image_transform.cpp:667`: フィルタ処理で8bit変換を経由（16bit直接処理への最適化が可能）

### 次のステップ（推奨）
1. このブランチを main にマージ
2. GitHub Actions デプロイ設定を main ブランチに変更
3. 必要に応じて追加のノードタイプを実装（例: ぼかしノード、トリミングノード等）
