# ブランチ戦略

## ブランチ構成

### mainブランチ
- **役割**: 安定版
- **更新方法**: PRマージ

### 作業ブランチ (claude/*)
- **役割**: 機能開発・バグ修正
- **命名規則**: `claude/機能名` または `claude/修正内容`
- **例**: `claude/feature-blur-filter`, `claude/fix-image-bug`

## 開発フロー

```
1. mainブランチから作業ブランチを作成
   git checkout main
   git checkout -b claude/feature-name

2. 開発・コミット・プッシュ
   git add .
   git commit -m "説明"
   git push -u origin claude/feature-name
   → 自動デプロイ実行

3. PR作成 → レビュー → mainにマージ
```

## 自動デプロイ

以下のブランチへのpushで自動ビルド＆デプロイが実行されます：
- `main`
- `claude/*`

デプロイ先: https://ainyan03.github.io/claude_code_test/

## 注意事項

- GitHub Pagesは最後にデプロイされたブランチが表示されます
- 本番環境と開発環境が同じURLを共有します

## 関連ドキュメント

- [PROJECT_STATUS.md](PROJECT_STATUS.md): プロジェクトの現在の状態
- [CHANGELOG.md](CHANGELOG.md): 変更履歴
- [README.md](README.md): プロジェクト概要
