# Repository rules for Claude

このファイルはこのリポジトリで Claude Code セッションが開始されるときに読み込まれます。以下のルールは必ず守ってください。

## Git push policy

`git push` を実行する前に必ず:
1. `git status` を実行し、出力をユーザーに見せる
2. `git log -1 --oneline` で送信される最新のコミットを表示
3. 現在のブランチが意図した push 先と一致するか確認
4. `main` ブランチにいるときだけ `origin/main` への push を許可
5. `main` 以外のブランチにいる場合は、ユーザーに確認を取るまで push しない

## 禁止フラグ（明示的な指示がない限り使わない）

- `git push --force` / `git push --force-with-lease`
- `git push --all`
- 異なるリモート (`git push <other-remote>`) への push

## ブランチの取り扱い

- 確認なくブランチを削除しない
- `main` ブランチで rebase / 履歴書き換えをしない
- `git reset --hard` は明示的に指示された場合のみ

## デプロイ環境

- `main` ブランチへの push は本番環境にデプロイされる: https://CCC2026.shironoir.com
- 他ブランチへの push は Cloudflare Pages のプレビュー環境のみに反映される

## 作業境界（Working Directory Boundaries）

- 作業ディレクトリ: `/Users/hi/Club-Avtivity/CCC2026/` のみ
- `cd` でリポジトリ外に移動しない
- 新しい remote を追加しない（明示的指示がある場合のみ）
- push 先は `origin` のみ
- `.private/` 配下は **絶対に GitHub に push しない**（メンバー個人情報・内部メモを含む）

## プロジェクト概要

CCC2026 電子工作班のドキュメントサイト。
2026年7月の地域お祭り（水辺で乾杯！利根運河）に出展する、子供向けミニゲームを制作するプロジェクト。

### サイト構成

- `/` ホーム（概要 + 作品一覧 + 班構成 + 出展先）
- `/cranegame` クレーンゲーム
- `/tasks` 各班別タスクボード（タブUI）
- `/ringtoss` バーチャル輪投げ（将来）
- `/racing` レースゲーム（将来）

### デザイン規約

- テーマカラー: `#EF8733`
- 絵文字は使用しない（テキスト・記号のみで装飾）
- タイポグラフィ主導、文章量は少なめ
- editorial / minimal トーン

### 役割（公開表記）

- メンター
- フレームワーク班
- コントローラー班
- ソフトウェア・アプリ制作班

**メンバー名は公開ドキュメントでは使わず、班名で表記する。**
内部メモは `.private/INTERNAL.md` 参照（gitignored）。
