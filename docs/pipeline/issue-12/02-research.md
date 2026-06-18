# 02-research.md

- **Issue:** #12 パイプ |(複数コマンドの pipe/fork 接続)
- **Stage:** 2/5 Investigator
- **実測環境:** macOS (Darwin 21.6.0 / Intel), Apple clang 14, 2026-06-17 にパイプライン本体で実測
- **方式:** Architect 要請の7点をプロトタイプ実コード + bash 参照で裏取り。

## 1. N 段 pipe の fd close 順序 — 【実測済み・デッドロック無し・リーク0】
設計 4-1/4-2 の擬似コードどおりに 3 段パイプライン(`echo pipe-ok | cat | cat`)を execve で実装し検証:
- 出力 `pipe-ok` + 親が `last_status=0` で正常終了、**ハングなし** ✅
- `leaks --atExit` → **`0 leaks for 0 total leaked bytes`** ✅
- **確定アルゴリズム(親):**
  ```
  in_fd = -1
  while (各 cmd):
      if (次がある) pipe(pfd); else pfd = {-1,-1}
      pid = fork(); 子は child_run(cmd, in_fd, pfd)
      if (in_fd != -1) close(in_fd)           # 前段読端を子へ渡し終えたら親は閉じる
      if (次がある) { close(pfd[1]); in_fd = pfd[0]; }  # 書端は即 close、読端を持ち回り
  if (in_fd != -1) close(in_fd)
  全 pid を waitpid、最終段 status を採用
  ```
- **子(child_run):** `in_fd`→dup2(STDIN)+close、`pfd[1]`→dup2(STDOUT)+close+**close(pfd[0])**(自段読端は使わない=EOF 保証)。
- **要点:** 「親は書端を fork 直後に close」「子は自段で使わない pipe fd を close」を守ると N 段でハング・リークなし。プロトタイプで実証。

## 2. 空コマンド境界(redir-only 段)— 【bash 実測】
| bash 入力 | 結果 |
|---|---|
| `> a \| b`(先頭段が argv 空・redir のみ) | **合法**(status 0、`b` 実行)|
- **確定:** 「argv 空でも **redirs があれば合法**」。**syntax error は argv も redirs も両方空のときのみ**(設計デフォルトどおり)。`p_pipe` の空コマンド判定は `argv[0]==NULL && redirs==NULL`。
- 先頭 `|` / 末尾 `|` / `| |`(間に何もない)は空コマンド → syntax error status 2。

## 3. パイプの終了ステータス — 【bash 実測】
- `false | true` → `$?`=**0**、`true | false` → `$?`=**1** ✅
- **確定:** **最終段の status のみ**を `last_status` に採用(pipefail はスコープ外)。`wait_status`(`execute.c`)を最終段 pid の status に適用。

## 4. パイプ内ビルトインがシェルに効かない — 【bash 実測】
- `cd /; echo x | cd /tmp; pwd` → `/`(cd は子で実行されシェルに**効かない**)✅
- **確定:** パイプライン内 builtin は子で `run_builtin` → `exit(shell->last_status)`。cd/export/unset/exit は子で完結し親シェルに伝播しない(bash 同等)。`bi_exit` は `exit()` 直呼びなので子だけ終了しシェル生存(追加対応不要)。

## 5. 全子 waitpid でゾンビ無し — 【実測】
- プロトタイプは全 pid を waitpid → `ps` で defunct なし(テスト harness の背景ジョブ由来の defunct を除き、プロトタイプ自身はゾンビを残さない)✅
- **確定:** pid を**配列で保持**(段数を parse 後に数えるか、`argv_push` 流用で grow)。途中段も含め全 waitpid。

## 6. redir / heredoc 優先 — 【設計確認】
- 子で **pipe の dup2 を先、その後 `apply_redirs`**(上書き)。`echo x | cat > out` は cat の stdout が out に上書き(pipe 書端を上書き)= bash 準拠。
- heredoc は**各子内で `run_heredocs` 先読み**(最小方針)。複数段 heredoc は子が順次 fork されるため readline が直列に走る。bash の「全 heredoc を実行前に一括先読み」とは異なるが、**最小実装で許容**(一括先読みは後続Issue)。

## 7. child_exec / wait_status の共有 — 【実装方針】
- `execute.c` の `child_exec`(execve + 127/126 + errno 分岐)と `wait_status` は現状 `static`。パイプ子でも execve / status 採取が要る。
- **推奨:** `wait_status` を**非 static 化**してヘッダ公開し共有(単純・重複なし)。外部コマンドの execve 部は **`child_exec` を非 static 化して共有**(ただし `child_exec` は単一コマンド用に `exit` する=パイプ子でもそのまま使える)。execute.c の関数数(現 cmd_error/child_exec/wait_status/run_external = 4)に余裕があり、公開しても file 5 関数上限内。
- → 実装は `child_exec(t_shell*, t_cmd*, char*)` と `wait_status(int)` をヘッダ公開し、`pipeline.c` の子側 external 経路と wait ループで流用。重複コードを避ける。

## 8. 既存コードとの接続(事実確認)
- `include/minishell.h`: `t_cmd` に `struct s_cmd *next`、`t_tok_type` に `TOK_PIPE` 追加。`exec_pipeline` / `free_pipeline` / (公開する)`wait_status` / `child_exec` のプロトタイプ。
- `src/lexer_op.c`: `is_op` に `|`、`op_len` に `|`=1 のガード、`op_type` に `TOK_PIPE`。`handle_none`/quote ハンドラは無変更で `echo "|"` リテラル。
- `src/parser.c`: `p_pipe` 新設(空コマンド検査 + `cur->next` 連結)、`parse_tokens` を cur 持ち回りに拡張。
- `src/process_line.c`: exec 分岐 `cmd->next ? exec_pipeline : exec_cmd`。`free_cmd` → `free_pipeline`。
- `src/cmd.c`: `cmd_new` で `next=NULL`、`free_pipeline` 新設。
- 新規 `src/pipeline.c`: exec_pipeline / pipe_child / wait_all 等。
- `src/execute.c`: `child_exec`/`wait_status` を非 static 化(プロトタイプ公開)。
- `Makefile`: SRCS に `src/pipeline.c`。

## 9. 42 Norm 注意
- `pipeline.c` は ≤5関数。exec_pipeline のループ(pipe/fork/親後始末)は25行超過しやすい → 1段分を `pipe_fork_one` に切り出し、`wait_all` を分離。
- `t_cmd.next` 追加で free_pipeline は while ループ(再帰不可ではないが while 推奨)。
- グローバル0個維持(pids は exec_pipeline のローカル配列)。三項/for/switch 禁止。

## 10. リスク箇所(上位3)
### リスク1: pipe fd の close 漏れ → デッドロック or fd リーク
- 回避: 設計アルゴリズム厳守(親=書端 fork 直後 close + 読端持ち回り、子=不要 fd 全 close)。プロトタイプで実証済み。Implementer は `cat|cat|cat` でハング無しと、多段後の fd 非増加を確認。

### リスク2: 全子 waitpid 漏れ → ゾンビ / status 誤り
- 回避: pid 配列に全 fork の pid を格納、全 waitpid。最終段 pid の status のみ last_status。

### リスク3: 単一コマンド経路の回帰(builtin 親実行が壊れる)
- 回避: `cmd->next==NULL` は **exec_cmd を一切変更しない**。cd/export/exit/redir/heredoc が #6/#10 のまま動くことを回帰確認。

## 11. Implementer が叩く検証コマンド
```bash
make
printf 'echo hello | cat\n' | ./minishell                 # hello
printf 'ls | wc -l\n' | ./minishell                       # 数値
printf 'ls | cat | wc -c\n' | ./minishell                 # 3連結
printf 'echo a | cat | cat\n' | ./minishell               # a(多段ハング無し)
printf 'echo hi | cat > /tmp/po\ncat < /tmp/po\n' | ./minishell  # redir優先
printf 'false | true\necho $?\ntrue | false\necho $?\n' | ./minishell  # 0 / 1
printf 'cd /tmp\npwd\nexport A=1\nenv | grep A\n' | ./minishell  # 単一builtin親実行回帰
printf '| cat\necho $?\necho |\necho $?\n' | ./minishell   # syntax error 2
printf 'echo "|"\n' | ./minishell                          # リテラル |
# 多段後 fd 非増加(外部で確認)/ ps で defunct 無し
/tmp/pdfvenv/bin/norminette src include libft
```

## 申し送り
1. **42 ヘッダー未付与**(ログイン待ち)継続。
2. **空コマンド判定** = `argv[0]==NULL && redirs==NULL`(redir-only 段は合法、bash 一致)。
3. **child_exec / wait_status を非 static 公開**して pipeline.c で共有(重複回避)。
4. **heredoc 各子先読み**(最小)/ **最終段 status のみ**(pipefail なし)はスコープ内固定。
5. fork 子のメモリリークは `leaks --atExit` が追えない(#10 同様)→ child は exit で回収、code review 担保。

## 関連ファイル
- 設計入力: `docs/pipeline/issue-12/01-design.md`
- 本成果物: `docs/pipeline/issue-12/02-research.md`
