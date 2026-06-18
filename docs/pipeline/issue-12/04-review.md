# 04-review.md

- **Issue:** #12 パイプ |(複数コマンドの pipe/fork 接続)
- **Stage:** 4/5 Reviewer
- **対象:** ブランチ `feat/issue-12-pipe` / コミット `3f07f73`
- **レビュー方式:** Implementer 報告を信用せず、メインが独立にビルド・Norm 監査・**敵対的エッジ**・fd/ゾンビ/デッドロック/回帰を再実行。

## 総評
**承認(差し戻し不要)。** must 級なし。`t_cmd` リスト化 + パイプライン実行の全 DoD を独立検証で確認。単一コマンドの builtin 親実行(cd/export/exit)も回帰維持。Norm は 42 ヘッダー(保留)を除きクリーン。should/既知の制限を文書化。

## 独立検証
| 観点 | 結果 |
|---|---|
| `make re` ビルド / 再リンクなし | ✅ |
| 関数行数(≤25)/ 5関数/file | ✅ 全ファイルで超過ゼロ(pipeline4/pipeline_child3/parser5/parser_pipe1/execute5/cmd5/lexer_op4/process_line4)|
| 禁止構文 / グローバル | ✅ なし / 0個 |
| norminette | ⚠️ `INVALID_HEADER` のみ(32ファイル)|
| メモリリーク(parse経路)| ✅ 0 leaks |
| fd リーク | ✅ `/dev/fd` が bash と同一 |
| **ゾンビ** | ✅ 稼働中 minishell の defunct 子 **0** |
| デッドロック | ✅ なし |

### 機能・エッジ(独立実行)
- `echo hello | cat` / `ls | wc -l`=6 / 3段 / **5段** `echo five|cat|cat|cat|cat`→`five` ✅
- `cat < f | wc -l`(先頭段入力 redir)/ `> a | echo hi`(redir-only 段=合法)✅
- `echo hi | cat > out`(redir 優先)✅
- `false|true`=0 / `true|false`=1(最終段 status)✅
- **`exit | cat`** → シェル生存(`after-still-alive` 出力)= bash の subshell 同等 ✅
- **`nosuchcmd | echo recovered`** → `recovered` + stderr に not found、`$?`=0(最終段成功)✅
- `seq 1 1000 | wc -l`=1000(大量出力でデッドロック/欠落なし)✅
- `echo "|"`→リテラル、`|cat`/`echo|`/`a||b`→syntax error status 2 ✅
- 回帰: 単一 builtin `cd /tmp;pwd`/`export REGR;env|grep`(親実行でシェル反映)、redirect/quote/`$`展開 不変 ✅

## 観点別レビュー

### 設計準拠
- ✅ `t_cmd.next` リスト化、`cmd->next ? exec_pipeline : exec_cmd`。**単一コマンド経路(exec_cmd)を温存**し cd/export/exit のシェル反映を維持。
- ✅ `|` トークン化(`||` を1文字ずつ=空コマンドエラー)、クォート内リテラル(handle_single/double 無変更)。
- ✅ パイプ内 builtin は子で run_builtin→exit、redir/heredoc は pipe dup2 後に適用(優先)。
- 妥当な微調整: `free_pipeline` を作らず `free_cmd` をリスト対応に / `child_exec`→`exec_external`(redir 分離で単一/パイプ共有、重複なし)。

### バグ / fd・ゾンビ
- 致命バグなし。**fd 管理が正しい:** `pipe_fork_one` で親は書端を fork 直後 close、前段読端 `in_fd` を1変数持ち回り、最終段で `*in_fd=-1`(post-loop 二重 close 回避)。子 `pipe_dup` は dup2 後に全 pipe fd close(EOF 保証=デッドロック回避)。`/dev/fd` が bash 同一で fd リークなしを実証。
- **全子 waitpid**(`wait_all`)でゾンビ0(稼働中 minishell の defunct 子0で実証)。最終段 status のみ採用。
- `exec_external` の path free / errno 退避(execve 直後)は #10 から維持。`pipe_child` の builtin 経路は `exit(last_status)`。

### セキュリティ
- ✅ 許可関数(pipe/fork/dup2/close/execve/waitpid/open)のみ。エラーは write 固定文字列。

### 可読性
- ✅ parser(分割)/ pipeline(親 fd 管理)/ pipeline_child(子)/ execute(共有 execve)が責務分割。fd の close 箇所がコメントで明示。

## 指摘ログ

### must — なし

### should / 既知の制限(別Issue・非ブロッキング)
1. **`exit | cat` が `exit` を出力** — bi_exit が非対話でも `exit\n` を書くため pipe 経由で cat が出力。#2 由来の既知 should(非対話の exit メッセージ)。シェル生存・動作は正しい。
2. **`pipe()`/`fork()` 失敗時の堅牢化が最小** — catastrophic 時に perror/部分 fd 回収を省略(pid<0 は waitpid がスキップ)。資源枯渇時のみで実害小。改善は後続。
3. **heredoc は各子先読み**(複数段 heredoc で readline が直列)/ **pipefail なし**(最終段のみ)/ 大容量 heredoc スコープ外 — 設計どおり最小。

### nice / 既知
1. **fork 子のメモリリーク**は `leaks --atExit` が fork 子の atexit でハングのため自動計測不可 → child は exit 回収、親 `free_cmd` + pids free を **code review で確認**(漏れなし)。

## 次段(Integrator)への申し送り
- 差し戻し往復: **0 回**(must なし)。段階5 へ進んでよい。
- 本ブランチは main 分岐 → PR ベースは **main**。
- PR 本文に「パイプが動く」「単一 builtin 親実行の回帰維持」「DoD 充足」「既知(exit表示/fork-leak review/heredoc各子先読み)」を明記。`Closes #12`。
- **マンダトリーで残るは シグナル(ctrl-C/ctrl-D/ctrl-\)のみ**。次の自然な単位。
