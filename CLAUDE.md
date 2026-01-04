# Claude Code プロジェクトガイドライン

## ブランチ運用規則

### 命名規則
- 作業用ブランチは `claude/` で始める名前を使用する
- 形式: `claude/<機能名>-<セッションID>` または `claude/<修正内容>`
- 例:
  - `claude/fix-odd-width-image-distortion`
  - `claude/feature-blur-filter-abc123`
  - `claude/refactor-viewport-stride`

### ワークフロー
1. mainブランチから新しい作業用ブランチを作成
2. 作業完了後、PRを作成してmainにマージ
3. マージ後、作業用ブランチを削除

## ビルド手順

```bash
cd image-transform-preview
source /path/to/emsdk/emsdk_env.sh  # Emscripten環境を読み込み
./build.sh
```

※ローカル環境固有の設定（emsdkパスなど）は `CLAUDE.local.md` に記載

## コード品質

- 変更前に必ずビルドが通ることを確認
- TODO.md で課題を管理
- コミットメッセージは変更内容を明確に記載
