# プロジェクトステータス

最終更新: 2026-01-05

## 🎯 現在の状態

**ステータス**: ✅ 安定版（Phase 6完了 + UI/UX大幅改善）

このプロジェクトは、レイヤーベースシステムからノードグラフエディタへの大規模リファクタリングを完了し、Phase 6でWebAssemblyバインディングの整理を完了しました。さらにUI/UXの大幅改善（左サイドバーメニュー、リサイズ可能なスプリッター、プレビュー倍率調整など）を実施しました。

## 📍 ブランチ構成と運用方針

### 現在のブランチ
- **mainブランチ**: ✅ 作成済み（安定版の基準ブランチ）
- **開発ブランチ**: `claude/*` 形式で各セッションごとに作成
- **状態**: ViewPort統一画像型への完全移行完了（Phase 5A-D）、Image16削除完了

### 運用戦略（ハイブリッド方式）
詳細は [BRANCH_STRATEGY.md](BRANCH_STRATEGY.md) を参照。

**自動デプロイ対象**:
- `main` ブランチ（安定版）
- `claude/*` すべてのブランチ（開発版）

**フロー**:
1. 各セッションで `claude/[feature]-[session-id]` で作業
2. pushすると即座にGitHub Pagesにデプロイ（ユーザーがすぐ確認可能）
3. 作業完了後、PRを作成してmainにマージ

## ✅ 完了した作業

### コア機能
- [x] ノードグラフエディタの実装
- [x] 画像ライブラリの実装
- [x] 5種類のノードタイプ（画像、アフィン変換、合成、フィルタ、出力）
- [x] C++ NodeGraphEvaluator の実装
- [x] トポロジカルソートによる依存関係解決
- [x] メモ化による重複計算回避
- [x] **拡張可能ピクセルフォーマットシステム**: Straight/Premultiplied alphaの自動管理
- [x] **数学的に正確なフィルタ処理**: レビュアー指摘事項の完全解決
- [x] **Lazy conversionパターン**: 不要な形式変換の最適化
- [x] **ViewPort統一画像型**: Image/Image16統合、組込み環境対応（Phase 5A-D完了）
- [x] **カスタムアロケータ統合**: DefaultAllocator/FixedBufferAllocator対応
- [x] **ビューポート/ROI機能**: ゼロコピーサブリージョン参照
- [x] **モジュラーファイル構造**: 関心の分離による保守性向上

### UI/UX
- [x] ドラッグ&ドロップでノード配置
- [x] ワイヤーによる接続UI
- [x] コンテキストメニュー（右クリック）
- [x] パラメータ/行列表示モード切り替え
- [x] 動的ノード高さ調整
- [x] ポート位置のラグ修正
- [x] リアルタイム接続線更新
- [x] **左サイドバーメニュー**: 画像ライブラリと出力設定を統合、トグルボタンで開閉
- [x] **リサイズ可能なスプリッター**: ノードグラフとプレビュー間のドラッグリサイズ
- [x] **ビューポート固定レイアウト**: ブラウザスクロール不要、各セクション独立スクロール
- [x] **プレビュー倍率調整**: 1x〜5xの表示倍率スライダー
- [x] **スクロール自動中央調整**: ノードグラフ・プレビュー両方に適用
- [x] **ヘッダーコンパクト化**: 横並びレイアウトでコンテンツ領域拡大
- [x] **レスポンシブデザイン改善**: PC/モバイルで適切なサイドバー動作

### ドキュメント
- [x] README.md の全面書き換え
- [x] QUICKSTART.md の更新
- [x] GITHUB_PAGES_SETUP.md の更新
- [x] CHANGELOG.md の作成
- [x] PROJECT_STATUS.md の作成（このファイル）

### 削除とリファクタリング
- [x] 旧レイヤーシステムの完全削除（461行）
- [x] 非推奨コードの削除（350行）
- [x] モノリシックファイルの分割（保守性向上）

### C++コード構造（モジュラー設計）
```
src/
├── image_types.h              # 基本型定義（Image, AffineParams, AffineMatrix等）
├── pixel_format.h             # ピクセルフォーマット記述子（Phase 1）
├── pixel_format_registry.h/cpp # フォーマットレジストリ（Phase 1）
├── image_allocator.h          # メモリ管理インターフェース（Phase 1）
├── viewport.h/cpp             # ViewPort統一画像型（Phase 5、Image16完全置換）
├── filters.h/cpp              # フィルタクラス群（ViewPortベース）
├── image_processor.h/cpp      # コア画像処理エンジン（ViewPortベース）
├── node_graph.h/cpp           # ノードグラフ評価エンジン（ViewPortベース）
└── bindings.cpp               # JavaScriptバインディング（NodeGraphEvaluatorのみ公開）
```

## 🔄 次のアクション（優先順位順）

### ViewPort統合とImage/Image16移行
1. ~~Phase 1: 基盤システムの実装~~: ✅ 完了（PixelFormatRegistry等）
2. ~~Phase 2: Image16構造体の拡張~~: ✅ 完了（formatIDフィールド追加）
3. ~~Phase 3: フィルタの修正~~: ✅ 完了（レビュー指摘事項解決）
4. ~~Phase 4: ノードグラフ評価の最適化~~: ✅ 完了（自動形式変換）
5. ~~**Phase 5A: ViewPort統一画像型の実装**~~: ✅ 完了（組込み環境対応基盤構築）
6. ~~**Phase 5B: ViewPortベース処理関数の実装**~~: ✅ 完了（filters、image_processor移行）
7. ~~**Phase 5C: 既存コードの段階的移行**~~: ✅ 完了（node_graph、bindings移行）
8. ~~**Phase 5D: Image16の完全削除**~~: ✅ 完了（Image16とヘルパー関数削除）
9. ~~**Phase 6: WebAssemblyバインディングの整理**~~: ✅ 完了（ImageProcessorWrapper削除、約410行削減）

**✅ Phase 6まで完全に完了！NodeGraphEvaluatorのみを使用する簡潔なアーキテクチャになりました。**

### 将来の機能拡張（任意）
- **Phase 7: 新フォーマットの実装**: RGB565、RGB332、インデックスカラー等
- **追加ノードタイプ**: トリミング、色調補正、その他のフィルタなど
- **ノードグラフの保存/読み込み**: JSON形式でのパイプライン保存機能
- **ユニットテスト**: PixelFormatRegistry、フィルタ処理のテストケース追加
- **モバイルUI最適化**: タッチ操作のさらなる改善

## 📦 ビルド成果物

現在のビルド成果物（web/ディレクトリ）:
- `image_transform.wasm`: WebAssemblyバイナリ（.gitignore対象）
- `image_transform.js`: Emscripten生成JavaScriptラッパー（.gitignore対象）
- `version.js`: ビルド情報（GitHub Actionsでビルド時に自動更新）
- `app.js`: フロントエンドロジック
- `index.html`: メインHTML
- `style.css`: スタイルシート

## 🚀 デプロイ

- **GitHub Pages**: https://ainyan03.github.io/claude_code_test/
- **自動デプロイ**: `main` または `claude/*` ブランチへのpushで自動実行
- **ビルドツール**: GitHub Actions + Emscripten
- **デプロイ時間**: 約3-5分（Emscriptenビルド含む）

## 🎯 最近解決した問題

### レビュアー指摘事項: プリマルチプライドアルファの不適切な使用
**問題**: ブライトネスやグレースケールフィルタがプリマルチプライドアルファデータに直接作用していた
**影響**: 数学的に不正確な結果（明るさ調整の誤差、グレースケール変換の暗化）
**解決策**: 拡張可能ピクセルフォーマットシステムの実装（Phase 1-4）
- フィルタはStraight alpha形式で処理（数学的に正確）
- 合成/変換はPremultiplied alpha形式で処理（ブレンディングに最適）
- 自動形式変換によりフィルタチェーン全体を最適化
**ステータス**: ✅ 完全解決（2026-01-03）

### Image/Image16の重複と組込み環境対応
**問題**: Image（8bit）とImage16（16bit）が別々の型として実装され、コードの重複とメンテナンス負荷が発生
**影響**:
- 各処理関数が両方の型に対応する必要がある
- std::vectorによるメモリ管理は組込み環境では不適切
- ROI処理でメモリコピーが発生（非効率）
**解決策**: ViewPort統一画像型の実装（Phase 5A-D）
- void*ポインタ + PixelFormatIDで任意の形式に対応（型に依存しない設計）
- ImageAllocatorインターフェースによるカスタムメモリ管理（組込み環境対応）
- 親子関係によるゼロコピーviewport/ROI機能
- RAII原則に従った安全なメモリ管理
**移行完了**: Image16は完全削除、内部処理はViewPortに統一
- Imageは8bit API境界のみで使用（WebAssembly bindings）
- すべてのフィルタ、画像処理、ノードグラフ評価がViewPortベースに
**ステータス**: ✅ Phase 5A-D完了（2026-01-03）

## ⚠️ 既知の問題と制約

### ブランチ管理の制約と解決策
1. **ブランチ名制約**: このリポジトリでは `claude/` で始まり、現在のセッションIDで終わるブランチ名のみpush可能
   - ✅ 可能（現在のセッション）: `claude/review-image-transform-preview-dZG9N`
   - ❌ 不可（403エラー）: `main`, `claude/main`
   - ✅ **解決済み**: GitHub Web UIで `main` ブランチを作成（UI経由の操作は制約を受けない）

2. **セッション間の運用フロー**: 新しいセッションでは異なるセッションIDが割り当てられる
   - ✅ **運用方法**: ハイブリッドブランチ戦略（詳細: [BRANCH_STRATEGY.md](BRANCH_STRATEGY.md)）
     1. `main` ブランチから新しい `claude/feature-name-[new-session-id]` を作成
     2. 開発中は `claude/*` にpush → 即座にデプロイ（ユーザーが確認）
     3. 作業完了後、PR作成 → `main` にマージ
     4. 次のセッションは `main` から継承して新しいブランチで作業

### デプロイの注意点
3. **GitHub Pagesは単一環境**: 本番環境と開発環境が同じURL
   - `main` または `claude/*` どちらのデプロイでも、最後にデプロイされたものが表示される
   - 開発中の変更が即座に公開される（これは意図した挙動）

## 📝 セッション継続性のための情報

このプロジェクトは複数のセッションにわたって開発されました。作業を再開する場合：

1. **最新の状態を確認**: このファイルと CHANGELOG.md を読む
2. **ブランチ戦略を理解**: [BRANCH_STRATEGY.md](BRANCH_STRATEGY.md) を読む
3. **コミット履歴を確認**: `git log --oneline -15` で最近の変更を確認
4. **ブランチ構造を確認**: `git branch -a` で開発ブランチを確認
5. **適切なブランチから開始**:
   - mainブランチが存在する場合: `git checkout main` → 新しいブランチ作成
   - mainブランチが存在しない場合: 最新の `claude/*` ブランチから継続
6. **次のアクションを実行**: 上記「次のアクション」セクションを参照

## 🔗 関連ドキュメント

### プロジェクト管理
- [PROJECT_STATUS.md](PROJECT_STATUS.md): このファイル（現在の状態と次のステップ）
- [CHANGELOG.md](CHANGELOG.md): 変更履歴の詳細
- [BRANCH_STRATEGY.md](BRANCH_STRATEGY.md): ブランチ戦略とセッション継続性

### ユーザー向け
- [README.md](README.md): プロジェクト概要と使い方
- [QUICKSTART.md](QUICKSTART.md): クイックスタートガイド
- [GITHUB_PAGES_SETUP.md](GITHUB_PAGES_SETUP.md): GitHub Pagesデプロイ手順
