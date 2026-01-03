# ブランチ戦略とセッション継続性

最終更新: 2026-01-02

## 🎯 基本方針

このリポジトリでは、**ハイブリッド運用**を採用しています：
- 開発中の成果物を即座に確認できる自動デプロイ
- mainブランチに安定版を蓄積する長期的な保守性
- セッション間の完全な継続性

## 🌳 ブランチ構成

### mainブランチ
- **役割**: 安定版の蓄積、長期的な履歴管理
- **作成方法**: GitHub Web UIで手動作成（初回のみ）
- **更新方法**: PRマージによる更新

### claude/*ブランチ
- **役割**: セッションごとの作業ブランチ
- **命名規則**: `claude/[feature-name]-[session-id]`
- **例**:
  - `claude/image-transform-preview-7sfw5`
  - `claude/feature-filters-8xyz9`
  - `claude/bugfix-ports-9abc1`
  - `claude/main-7sfw5`

## 🔄 開発フロー

### 各セッションでの作業

```
1. 新しいセッション開始
   ↓
2. mainブランチ（存在する場合）または前セッションのブランチから作業開始
   ↓
3. claude/feature-name-[session-id] で開発
   ↓
4. pushすると自動的にビルド＆デプロイ
   ↓
5. GitHub Pages (https://ainyan03.github.io/claude_code_test/) で即座に確認
   ↓
6. 作業完了後、PRを作成
   ↓
7. ユーザーがPRをレビュー＆マージ（mainに統合）
```

### 自動デプロイの仕組み

GitHub Actions (`.github/workflows/deploy.yml`) の設定：

```yaml
on:
  push:
    branches:
      - main          # 本番環境（安定版）
      - 'claude/*'    # 開発環境（即座に確認）
```

**どちらのブランチへのpushでも自動デプロイされます。**

## 📋 ブランチ名の制約

このリポジトリでは、Claudeのセッション管理により以下の制約があります：

### Push可能なブランチ名
- `claude/` で始まり、**セッションID**で終わる必要があります
- 例: `claude/feature-xyz-7sfw5`（セッションID: 7sfw5）

### Push不可能なブランチ名
- `main`（セッションIDなし）→ 403エラー
- `claude/main`（セッションIDなし）→ 403エラー
- `feature-xyz`（claude/ プレフィックスなし）→ 403エラー

### 解決策
- **mainブランチ**: GitHub Web UI経由で手動作成（ブランチ名制約の影響を受けない）
- **開発ブランチ**: 常に `claude/*-[session-id]` 形式を使用

## 🚀 実践的な運用例

### ケース1: 新機能の追加（セッションID: 7sfw5）

```bash
# 1. 新しいセッション開始（AIアシスタント）
git checkout -b claude/feature-blur-filter-7sfw5

# 2. 機能実装
# （コード編集）

# 3. コミット＆プッシュ
git commit -m "Implement blur filter node"
git push -u origin claude/feature-blur-filter-7sfw5

# → 自動的にGitHub Actionsが実行
# → 数分後、GitHub Pagesで確認可能

# 4. ユーザーが確認
# https://ainyan03.github.io/claude_code_test/ にアクセス

# 5. PRを作成（ユーザー操作、GitHub Web UI）
# claude/feature-blur-filter-7sfw5 → main

# 6. マージ（ユーザー操作）
# → mainブランチに統合
```

### ケース2: 次のセッションでの作業（セッションID: 8xyz9）

```bash
# 1. 新しいセッション開始
# mainブランチが存在する場合
git fetch origin
git checkout main
git checkout -b claude/feature-export-json-8xyz9

# mainブランチが存在しない場合
# 前のブランチから継続
git checkout -b claude/feature-export-json-8xyz9

# 2. 作業（以下同様）
```

## 📝 セッション継続のベストプラクティス

### セッション開始時（AIアシスタント）

1. **ブランチ構造を確認**
   ```bash
   git branch -a
   git log --oneline --graph --all -10
   ```

2. **ドキュメントを読む**
   - `PROJECT_STATUS.md`: 現在の状態と次のステップ
   - `CHANGELOG.md`: 変更履歴
   - `BRANCH_STRATEGY.md`: このファイル

3. **適切なブランチから開始**
   - mainがあればmainから
   - なければ最新の claude/* ブランチから

### セッション終了時（AIアシスタント）

1. **すべての変更をコミット＆プッシュ**

2. **ドキュメントを更新**
   - `PROJECT_STATUS.md`: 完了した作業を記録
   - `CHANGELOG.md`: 必要に応じて追記

3. **PRの作成を推奨**（ユーザーに報告）
   - ブランチ名と実装内容を伝える

## 🎯 この戦略のメリット

### 開発者（AIアシスタント）視点
- ✅ セッションIDが変わっても新しいブランチで継続可能
- ✅ pushすれば即座にデプロイされる（迅速なフィードバック）
- ✅ mainブランチから常に最新の安定版を継承できる

### ユーザー視点
- ✅ 作業中の成果物をリアルタイムで確認できる
- ✅ PRレビューで最終確認（デプロイ済みなので待ち時間なし）
- ✅ mainブランチに安定版が蓄積される

### プロジェクト視点
- ✅ 長期的な履歴管理（mainブランチ）
- ✅ セッション間の完全な継続性
- ✅ 標準的なGitワークフローとの互換性

## ⚠️ 注意事項

1. **GitHub Pagesは常に最新のデプロイを表示**
   - mainブランチのデプロイでも、claude/*ブランチのデプロイでも、最後にデプロイされたものが表示される
   - 本番環境と開発環境が同じURLを共有する

2. **mainブランチの初回作成はユーザー操作が必要**
   - GitHub Web UIで `claude/main-7sfw5` または任意の claude/* ブランチから作成
   - この操作は1回のみ

3. **セッションIDの変更**
   - 新しいセッションでは異なるセッションIDが割り当てられる
   - 前のセッションのブランチには直接pushできない（PRで対応）

## 🔗 関連ドキュメント

- [PROJECT_STATUS.md](PROJECT_STATUS.md): プロジェクトの現在の状態
- [CHANGELOG.md](CHANGELOG.md): 変更履歴の詳細
- [README.md](README.md): プロジェクト概要と使い方
