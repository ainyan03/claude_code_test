# GitHub Pages セットアップガイド

このプロジェクトは、GitHub Actionsで自動的にWebAssemblyをビルドし、GitHub Pagesで公開されます。

## 初回セットアップ手順

### 1. GitHub Pagesを有効化

1. GitHubリポジトリページを開く
2. **Settings** タブをクリック
3. 左サイドバーの **Pages** をクリック
4. **Source** セクションで **GitHub Actions** を選択
5. **Save** をクリック

### 2. ワークフローを実行

コードをプッシュすると、自動的にビルド＆デプロイが開始されます：

```bash
git add .
git commit -m "Add changes"
git push
```

または、手動で実行：

1. GitHubリポジトリの **Actions** タブを開く
2. **Build and Deploy to GitHub Pages** ワークフローを選択
3. **Run workflow** ボタンをクリック
4. ブランチを選択して **Run workflow** を実行

### 3. デプロイ状況を確認

1. **Actions** タブでワークフローの進行状況を確認
2. 完了すると緑のチェックマークが表示されます
3. **Settings > Pages** でデプロイされたURLを確認

## 公開URL

デプロイが完了すると、以下のURLでアクセス可能になります：

```
https://[ユーザー名].github.io/[リポジトリ名]/
```

例: `https://ainyan03.github.io/claude_code_test/`

## 自動ビルド＆デプロイの仕組み

### トリガー

以下の場合に自動的にビルド＆デプロイされます：

1. `main` ブランチにプッシュしたとき
2. `claude/*` ブランチにプッシュしたとき
3. GitHub上で手動実行したとき

### ビルドプロセス

1. **Emscriptenセットアップ**: 最新版のEmscripten SDKをインストール
2. **WebAssemblyビルド**: C++コードを `fleximg.wasm` にコンパイル
3. **成果物確認**: WASMファイルが正常に生成されたか検証
4. **GitHub Pagesデプロイ**: `fleximg/demo/web/` ディレクトリを公開

### ビルド時間

通常、3〜5分程度でビルド＆デプロイが完了します。

## トラブルシューティング

### ビルドが失敗する

1. **Actions** タブでエラーログを確認
2. C++コードにコンパイルエラーがないか確認
3. `build.sh` の実行権限を確認

### ページが表示されない

1. **Settings > Pages** で正しく設定されているか確認
2. Source が **GitHub Actions** になっているか確認
3. ワークフローが正常に完了しているか確認
4. ブラウザのキャッシュをクリア（Ctrl+Shift+R / Cmd+Shift+R）

### WebAssemblyが動作しない

1. ブラウザのコンソール（F12）を開いてエラーを確認
2. CORS エラーが出ていないか確認
3. WebAssemblyファイル（fleximg.wasm）が正しく読み込まれているか確認
4. ブラウザがWebAssemblyに対応しているか確認（Chrome 57+、Firefox 52+、Safari 11+）

## 更新方法

コードを変更してプッシュするだけで、自動的に再ビルド＆再デプロイされます：

```bash
# コードを編集
git add .
git commit -m "Update image transformation logic"
git push

# 自動的にビルド＆デプロイされます（3〜5分後に反映）
```

## Tips

### キャッシュのクリア

ブラウザがキャッシュした古いバージョンを表示している場合：
- PC: `Ctrl+Shift+R` (Mac: `Cmd+Shift+R`)
- スマホ: ブラウザの設定でキャッシュをクリア

### ビルドログの確認

詳細なビルドログを見るには：
1. **Actions** タブを開く
2. 該当のワークフロー実行をクリック
3. **build** ジョブをクリック
4. 各ステップを展開してログを確認

## 関連ファイル

| ファイル | 役割 |
|---------|------|
| `.github/workflows/deploy.yml` | GitHub Actionsワークフロー定義 |
| `fleximg/build.sh` | WebAssemblyビルドスクリプト |
| `fleximg/demo/web/` | デプロイされるWebアプリケーション |
