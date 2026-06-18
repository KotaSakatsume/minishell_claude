# 01-design.md

- **Issue:** #12 パイプ `|`(複数コマンドの pipe/fork 接続 / パイプライン実行)
- **Stage:** 1/5 Architect

## 方針 (1行)
`t_cmd` に `next` を足して `|` 区切りの**単方向リストにパイプライン化**し、レキサに `TOK_PIPE` を、パーサに「`|` で cmd を切り替える」ロジックを追加。実行は **単一コマンド(next==NULL)は現状の exec_cmd 経路を温存**(builtin 親実行で cd/export/exit が効く)、**複数コマンドは新設 exec_pipeline** が N-1 本 pipe を張り各段 fork、子で dup2→不要 fd 全 close→(heredoc/apply_redirs で上書き)→builtin or execve、親は全 fork 後に全 pipe fd を close し全子 waitpid で最後の status を採る。

## 設計方針 (5-7行)
- **アーキテクチャ:** レキサは NONE 状態でのみ `|`(1文字)を `TOK_PIPE` として emit(クォート内はリテラルのまま=`handle_single/double` 無変更)。パーサは現 `parse_tokens` の「単一 cmd を while で埋める」を「`|` 検出で `cmd->next` を新設し以降をそこへ埋める」に拡張。
- **データフロー:** `process_line → lex_tokens(TOK_PIPE 含む) → parse_tokens(→ t_cmd リスト) → exec_dispatch:(next==NULL? exec_cmd : exec_pipeline) → free_pipeline(リスト全解放)`。
- **主要インターフェース:** `t_cmd { char **argv; t_redir *redirs; struct s_cmd *next; }`、`int exec_pipeline(t_shell*, t_cmd*)`、子側 `void pipe_child(t_shell*, t_cmd*, int in_fd, int *pfd)`(in_fd=前段読端 / pfd=自段 pipe or NULL)。既存 `run_heredocs / apply_redirs / run_builtin / find_command_path / wait_status` を流用。
- **DB変更:** なし(該当しない)。
- **エラーハンドリング:** 構文エラー(先頭 `|` / 末尾 `|` / `||` 連続=空コマンド)は `parse_tokens` が `parse_error`(既存)経由で `last_status=2` を立て NULL を返し非実行。`pipe()`/`fork()` 失敗は親で perror + `last_status=1`、ここまでに張った fd と既 fork 子を回収してから中断。
- **fd 不変条件:** 全 pipe の両端は「使う直後に閉じる」を徹底し、子は execve/exit 前に**自分が使う dup2 済み fd 以外のすべての pipe fd を close**、親は**全 fork 完了後に保持中の pipe fd を全 close**してから waitpid(これがデッドロック/リーク回避の核)。

## 採用理由とトレードオフ
- **採用:** `t_cmd` を `next` 連結リスト化 + 実行を「単一(温存)/ パイプライン(新設 exec_pipeline)」に二分岐。理由: 単一コマンドの builtin 親実行(cd 等のシェル状態反映)を**1行も触らず**温存でき、パイプライン側を独立追加できるため既存回帰リスク最小。
- **却下A:** `t_cmd` を最初から配列(`t_cmd **` + count)化。理由: ランダムアクセスは不要で、配列化は cmd_new/argv_push/free を全面改修し #10 の単一前提コードを破壊する。リストなら `next` 追加と free 拡張だけで済む。
- **却下B:** パイプライン内でも builtin を親実行し、`|` のたびに dup2 で親 fd を退避/復元。理由: 親 1 プロセスで多段パイプを直列化するのは fd 退避地獄かつ並行実行にならず `yes | head` 等が成立しない。**パイプライン内 builtin は子で実行 + exit** が bash 準拠で素直(cd 等が外に効かないのも bash と同じ)。

---

## 1. データ構造変更

### 1-1. `t_cmd` に next 追加(`include/minishell.h`)
```
typedef struct s_cmd
{
    char        **argv;
    t_redir     *redirs;
    struct s_cmd *next;   /* ← 追加。パイプライン。単一なら NULL */
}   t_cmd;
```
- `cmd_new`(`src/cmd.c`)で `cmd->next = NULL` を初期化(1行追加)。

### 1-2. `TOK_PIPE`(`include/minishell.h` の enum)
```
typedef enum e_tok_type
{
    TOK_WORD,
    TOK_IN,
    TOK_OUT,
    TOK_APPEND,
    TOK_HEREDOC,
    TOK_PIPE      /* ← 追加 */
}   t_tok_type;
```

### 1-3. free 経路
- **新設 `free_pipeline(t_cmd *cmd)`**(`src/cmd.c`):`while (cmd) { next=cmd->next; free_cmd(cmd); cmd=next; }`。`free_cmd` は単一解放のまま不変(next を辿らない)。
- `process_line` の `free_cmd(cmd)` を **`free_pipeline(cmd)` に差し替え**(単一でも next==NULL で 1 周なので安全)。
- プロトタイプ `void free_pipeline(t_cmd *cmd);` を `cmd.c` 群に追加。

---

## 2. lexer / parser 拡張

### 2-1. `|` のトークン化(`src/lexer_op.c`)
- `is_op`:`return (c == '<' || c == '>' || c == '|');`(`|` を追加)。
- `op_len`:`|` は常に 1 文字。現行 `if (s[i+1]==s[i]) return 2;` は `||` を 2 と誤判定するため、**`|` は先頭で 1 を返す**ガードを足す(`if (s[i]=='|') return (1);`)。
- `op_type`:先頭で `if (s[i]=='|') return (TOK_PIPE);` を足す。
- `emit_op` は無変更(finish_word→tok_push の流れがそのまま使える)。
- `handle_none`(`src/lexer_state.c`)の `is_op` 分岐がそのまま `|` も拾うため**変更不要**。`echo "|"` は handle_double 内でリテラルとして buf に積まれ無問題(無変更で達成)。

### 2-2. `|` で cmd を分割(`src/parser.c`)
- 現 `parse_tokens` の while は単一 `cmd` を埋める。これを **`cur`(現在埋め中の cmd)を持ち、`TOK_PIPE` 検出で「直前 cmd が空でないか検証 → 新 cmd を `cur->next` に繋ぎ cur を進める」** に拡張。
- 関数分割(Norm 25行対策):`parse_tokens` 本体 / `p_pipe(t_shell*, t_cmd **pcur, ...)`(空コマンド検査 + 新 cmd 連結)を新設。`p_word / p_redir / redir_push / parse_error` は無変更で `cur` に対し動く。

### 2-3. syntax error 条件(全て `parse_error` 経由 → `last_status=2` / 非実行)
- **先頭 `|`**:最初のトークンが `TOK_PIPE`(`|` の前にコマンド無し)。
- **末尾 `|`**:`TOK_PIPE` の次が無い(`toks->next == NULL`)。
- **`||` 連続 / `| |`**:`TOK_PIPE` 検出時、**直前 cmd の argv が空かつ redirs も空**(=空コマンド)。※`> a | b` のような「リダイレクトのみの段」は bash では合法だが、本 Issue は**最小で「argv 空 = エラー」**とするか「redir があれば許容」とするかを Investigator が bash 挙動で裏取りし確定(下記裏取り依頼参照)。デフォルト方針は **bash 準拠で「argv も redirs も空のときのみエラー」**。

---

## 3. exec_dispatch(分岐点 / `src/process_line.c`)
```
exec_cmd(shell, cmd):                 /* 既存。単一コマンド用。変更しない */
    run_heredocs → redir_only / builtin_redir / run_external

新:  if (cmd->next == NULL) return exec_cmd(shell, cmd);
     else                   return exec_pipeline(shell, cmd);
```
- `process_line` の `exec_cmd(shell, cmd)` 呼び出しを上記分岐に置換(1 行 →数行)。
- **単一コマンドは exec_cmd を 1 文字も触らない**(builtin 親実行 / cd・export・exit のシェル反映 / リダイレクト / heredoc が全て温存される)。

---

## 4. exec_pipeline の詳細(新ファイル `src/pipeline.c`)

### 4-1. fd の正確な順序(擬似コード / デッドロック・リーク回避)
```
exec_pipeline(shell, head):
    in_fd = -1                 /* 先頭段の stdin は継承(リダイレクトない限り) */
    pids  = 確保 or 連結リスト  /* 全子 pid を保持(全 waitpid 用) */
    cur = head
    while (cur):
        if (cur->next):        /* 最終段以外は pipe を張る */
            pipe(pfd)          /* pfd[0]=読端, pfd[1]=書端 */
        else:
            pfd = {-1,-1}
        pid = fork()
        if (pid == 0):
            pipe_child(shell, cur, in_fd, pfd)   /* return しない(execve/exit) */
        /* --- 親側 fd 後始末(各段ごと即 close)--- */
        if (in_fd != -1) close(in_fd)            /* 前段読端は子へ渡し終えたので親は閉じる */
        if (cur->next):
            close(pfd[1])      /* 親は書端不要。即閉じ(子だけが書く)*/
            in_fd = pfd[0]     /* 次段の読端として保持(まだ閉じない)*/
        pids に pid を追加
        cur = cur->next
    /* 全 fork 完了 → 親に残る pipe fd は in_fd のみ(最終段で消費済みなら -1)。念のため close */
    if (in_fd != -1) close(in_fd)
    wait_all(pids, &last)      /* 全子 waitpid、最終段 status を last_status へ */
```

### 4-2. 子の fd(`pipe_child`、`src/pipeline.c`)
```
pipe_child(shell, cmd, in_fd, pfd):
    if (in_fd != -1):
        dup2(in_fd, STDIN_FILENO); close(in_fd)
    if (pfd[1] != -1):                 /* 最終段以外 = 書端あり */
        dup2(pfd[1], STDOUT_FILENO)
        close(pfd[1])
        close(pfd[0])                  /* この段の読端は使わない → 必ず閉じる(EOF 保証)*/
    /* heredoc 先読み + リダイレクトは pipe より優先(dup2 後に上書き)*/
    if (run_heredocs(shell, cmd)) exit(1)
    if (apply_redirs(cmd->redirs)) exit(1)
    if (!cmd->argv[0]) exit(0)         /* redir のみの段 */
    if (is_builtin(cmd->argv[0])):
        run_builtin(shell, cmd->argv)
        exit(shell->last_status)       /* execve しないので明示 exit 必須 */
    /* 外部: find_command_path → execve(失敗時 127/126 で exit)*/
    child_exec 相当(execve / no-return)
```

### 4-3. デッドロック・fd リーク回避の要点(Implementer 必読)
- **親は各段の書端 `pfd[1]` を fork 直後に必ず close**。閉じ忘れると最終段の read が EOF を受け取れず**ハング**。
- **子は自段で使わない pipe fd(直前段の `pfd[0]` 相当=自分の読端でない側、および書端を持たない最終段)を全 close**。
- **親が保持するのは「次段に渡す前段読端 in_fd」1 本のみ**。それを次のループ先頭で子へ dup2 後に close、最後はループ後に close。
- **重要な単純化:** 各段で pipe は **1 本だけ**親が持つ設計(前段読端を変数 1 個で持ち回る)にすることで、「複数 pipe fd の取り違え」を構造的に排除する。N 段でも親が同時に持つ pipe fd は最大 2 本(新規 pfd[0]/pfd[1] + 引き継いだ in_fd は fork 前に確定)で、各段ループ末で 1 本に収束。

### 4-4. ファイル分割と関数割当(Norm: 1 file 5 関数 / 25 行)
| ファイル | 関数(≤5) | 役割 |
|---|---|---|
| `src/pipeline.c` | `exec_pipeline` / `pipe_child` / `wait_all` / `pipe_fork_one`(任意で 1 段分の fork+親後始末を切り出し 25 行内に収める) / `pl_error`(pipe/fork 失敗時の fd・子回収) | パイプライン本体 |
| `src/pipeline_child.c`(行数が溢れる場合) | `pipe_child` / `child_external`(execve 部) | 子側のみ分離(`child_exec` の execve ロジックを execute.c から共有 or 複製) |

- `child_exec`(`src/execute.c`)の execve 部はパイプ子でも使いたいが `static`。**プロトタイプ非公開を崩さず**、execve ロジックを `pipeline_child.c` に薄く複製するか、`execute.c` 側で `int child_external(t_shell*, t_cmd*, char*)` を非 static 化してプロトタイプ公開し共有する(Investigator が重複コードと公開のどちらが Norm/保守上良いか判定)。
- `Makefile` の `SRCS` に `src/pipeline.c`(必要なら `src/pipeline_child.c`)を追加。**ヘッダに `exec_pipeline` / `free_pipeline` のプロトタイプ追加**。

---

## 5. パイプライン内ビルトインの子実行
- パイプライン内では `is_builtin` でも**親実行しない**。子(`pipe_child`)で `run_builtin(shell, cmd->argv)` を呼び、直後に `exit(shell->last_status)`。
- 子で `run_builtin` が更新した `shell->last_status` は**親には伝播しない**(別プロセス)。親は waitpid の終了コードから status を再構築する(4-7 と一致)。これは bash 同等(`echo x | cd /` の cd は呼び出し側 shell に効かない)。
- 子のメモリ(argv / cmd / env)は `exit` で OS が回収。fork 子のリークは leaks 検査対象外(#10 と同じ前提)。
- 注意:`exit` ビルトインがパイプライン内にあっても**子だけが終了**しシェルは生存(bash 同等)。`bi_exit` が `exit()` を直接呼ぶ実装なら子で完結するため追加対応不要(Investigator が `bi_exit` の挙動を確認)。

---

## 6. リダイレクト / heredoc との統合(pipe より優先)
- 子は **pipe の dup2 を先に行い、その後 `run_heredocs` → `apply_redirs`** を呼ぶ。これにより `cmd1 | cmd2 > out` は cmd2 の stdout が pipe ではなく out に上書きされる(=リダイレクト優先、bash 準拠)。
- heredoc は **各コマンドで子内 `run_heredocs` で先読み**。`<< EOF` を含む段はその子の中で readline ループが走る。※複数段に heredoc がある場合、子は順次 fork されるため**各 heredoc の readline が直列に親→子で走る**ことになる。bash は全 heredoc を実行前に読む。**最小方針:各子内で先読み**(本 Issue スコープで許容)。より bash 厳密にするなら「全 heredoc を fork 前に親で先読みし fd を持たせる」だが**スコープ外**(後続 Issue / 裏取りで現状方式の問題有無を確認)。
- `cmd->redirs == NULL` の段はそのまま pipe の dup2 のみ有効。

---

## 7. status 採取と全子 waitpid(ゾンビ防止)
- **全子を waitpid する**(途中段も含む)。waitpid 漏れ = ゾンビ。
- **最終段(`next==NULL` の cmd に対応する pid)の終了コードを `last_status` に採用**。途中段の status は捨てる(bash 同等。`pipefail` はスコープ外)。
- 実装:`wait_all(pids, &last)` が pid リスト/配列を順に waitpid し、最終段 pid のときだけ `wait_status(status)`(既存 `execute.c`、static なら共有のためプロトタイプ公開 or 複製)を `last` に格納。最後に `shell->last_status = last`。
- pid の保持方法は **可変長配列(段数を parse 後に数える)** か **小さな連結リスト**。Norm の grow ロジックは `argv_push_cmd` 流用が楽。Investigator が段数カウント関数の要否を確認。

---

## 8. メモリ / fd 管理の不変条件(Reviewer チェック軸)
1. **全 pipe の両端が必ず close される**:書端は親 fork 直後 / 子は dup2 後 / 読端は親が次段へ渡した後 or ループ後。N 段で leak 0。
2. **子は dup2 済み標準 fd 以外の pipe fd を 0 本にしてから execve/exit**(EOF 伝播 = デッドロック回避)。
3. **全子 pid を waitpid**(ゾンビ 0)。
4. **メモリ:親プロセスは `free_pipeline` で argv/redir/cmd/next を全解放**。pids 配列/リストも解放。
5. **heredoc fd**:子内で open → apply_redirs/run_one_heredoc が close。親側に heredoc fd を残さない(各子先読み方式なので親は触らない)。
6. **エラー時(pipe/fork 失敗)も上記不変条件を維持**:`pl_error` が張り済み fd を close し、既 fork 子を waitpid してから戻る。

---

## 9. エラー / スコープ / やらないこと / 後続

### 9-1. エラー(syntax error → status 2 / 非実行)
- 先頭 `|` / 末尾 `|` / `||`(空コマンド)= `parse_error` 経由で `last_status=2`、NULL 返却で非実行。メッセージは既存 `syntax error near unexpected token` を流用。

### 9-2. スコープ(影響範囲 / 想定変更行のオーダー)
| 対象 | 変更内容 | オーダー |
|---|---|---|
| `include/minishell.h` | `next` / `TOK_PIPE` / プロト 3 本 | +6 行 |
| `src/lexer_op.c` | `|` を is_op/op_len/op_type へ | +6 行 |
| `src/cmd.c` | `cmd_new` next 初期化 / `free_pipeline` 新設 | +12 行 |
| `src/parser.c` | `p_pipe` 新設 + `parse_tokens` 拡張 | +25 行 |
| `src/process_line.c` | exec_dispatch 分岐 | +5 行 |
| `src/pipeline.c`(新) | exec_pipeline / pipe_child / wait_all 等 | +90〜120 行 |
| `src/execute.c` | child_external / wait_status 共有のため非 static 化(任意) | +0〜6 行 |
| `Makefile` | SRCS に pipeline.c | +1 行 |
- **合計 ~150〜180 行 / 1 PR で完結するサイズ**。これ以上膨れる場合は「lexer/parser 拡張」と「exec_pipeline」を別 PR に分割提案。

### 9-3. やらないこと(3 点 + 明示スコープ外)
1. **シグナル**(ctrl-C 中断 / ハンドラ / グローバル変数)— 別 Issue。今回 `last_status` の `128+sig` 構築だけは `wait_status` 流用で自然に入るが、ハンドラ設置はしない。
2. **`&&` / `||` / サブシェル `()` / ワイルドカード / `2>`** — 構文・実行とも対象外。
3. **`pipefail` 相当 / 途中段 status 採取 / 全 heredoc の fork 前一括先読み** — 最終段 status のみ・heredoc は各子先読みで最小実装。bash 厳密化は後続。

### 9-4. 後続(シグナルをどう乗せるか / 見通し)
- シグナル Issue では `exec_pipeline` の親 waitpid ループで `SIGINT`/`SIGQUIT` を子のみに効かせ、親はハンドラを `SIG_IGN`(子は `SIG_DFL`)に切る形が自然。`pipe_child` 内で execve 前に `signal` 復帰を入れる枠を残しておく(本 Issue では触らない)。グローバル禁止の制約は `g_signal` 1 個のみ許容される 42 規約に従い別 Issue で導入。

---

## 10. 完了条件(DoD)— Implementer / Reviewer チェックリスト
- [ ] `echo hello | cat` が `hello` を出力(2 段、基本動作)。
- [ ] `ls | wc -l` が行数を出力(外部 | 外部、pipe 接続正)。
- [ ] `ls | cat | wc -c` の **3 連結**が動く(N≥3、in_fd 持ち回り正)。
- [ ] `cat | cat | cat`(`a|cat|cat` 系)で stdin → 多段 → 出力が EOF まで通り**ハングしない**(デッドロック無し)。
- [ ] `echo hi | cat > out.txt` で **リダイレクトが pipe より優先**(out.txt に hi、画面に出ない)。
- [ ] `cat < in.txt | wc -l` で先頭段の入力リダイレクトが効く。
- [ ] **最後の status**:`false | true` → `$?`=0、`true | false` → `$?`=1(最終段採用)。
- [ ] **パイプ内 builtin**:`echo x | cat` の echo(子実行)/ `ls | wc -l`、`echo a | export A=1`(export は子で完結し親に効かない=bash 同等)。
- [ ] **単一 builtin の親実行回帰**:`cd /tmp` 後 `pwd` が `/tmp`、`export A=1` が `env` に反映、`exit` でシェル終了(パイプ無しは exec_cmd 温存)。
- [ ] **既存回帰**:リダイレクト(`> >> < <<`)/ クォート(`'` `"`)/ 展開(`$VAR` `$?`)が #8/#10 のまま動く。
- [ ] **syntax error**:`| cat` / `echo |` / `echo || cat`(空コマンド)が `status 2` + メッセージ + 非実行。
- [ ] `echo "|" ` がリテラル `|` を出力(クォート内は演算子化しない)。
- [ ] **leaks 0**(親プロセス。valgrind/leaks で確認。fork 子は対象外)。
- [ ] **fd リーク 0**(`ls /proc/self/fd` 相当 or 多段ループ後に fd 増えない)。
- [ ] **ゾンビ 0**(全子 waitpid。`ps` で defunct 無し)。
- [ ] **Norm 準拠**(25 行 / 5 関数 / グローバル 0 / 80 列 / for・三項・switch 無し)。
- [ ] **再リンク無し**(`make` 後 `make` で再ビルドが走らない=依存正)。

---

## 後続 Investigator(Stage 2)への裏取り依頼
以下を**実コードで**確認し、設計の前提を確定させること(設計ではなく事実確認):

1. **N 段 pipe の fd close 順序**:4-1/4-2 の擬似コードどおりに `cat | cat | cat`(3 段以上)で実際に**デッドロックしない / fd リークしない**か。特に「親が書端を fork 直後に close」「子が自段で使わない pipe fd を全 close」が抜けると即ハング/リークするので、最小 PoC か既存ビルドで検証。
2. **パイプ内 builtin の子 exit**:`run_builtin` を子で呼んだ後 `exit(shell->last_status)` で status が正しく親に伝わるか。`bi_exit` がパイプライン内で呼ばれたとき**子だけ終了**しシェルが生存するか(`bi_exit` の `exit()` 直呼びの有無を確認)。
3. **最終段 status 採取**:`false | true`=0 / `true | false`=1 が waitpid の最終段から正しく構築されるか。`wait_status`(`execute.c`、static)をパイプライン側で使うため、**非 static 化して共有 vs 複製**のどちらが Norm/保守上適切か判定。
4. **全子 waitpid でゾンビ無し**:途中段も含め全 pid を waitpid しているか、`ps`/`defunct` 検査で確認。pid 保持(配列 vs リスト)と段数カウント関数の要否を確認。
5. **redir / heredoc 優先**:子で「dup2(pipe) → apply_redirs」順により `echo x | cat > out` が out に書かれるか。複数段 heredoc(`<<`)を**各子先読み**したときの readline 挙動(プロンプト多重 / 順序)が許容範囲か、bash 厳密化が後続必須かを判定。
6. **空コマンド判定の境界**:`> a | b`(リダイレクトのみの段)を bash は許すか。本設計デフォルト「argv も redirs も空のときのみ syntax error」が bash 挙動と一致するか実機確認し、parse の空コマンド条件を確定。
7. **`child_exec` 共有可否**:`execute.c` の `child_exec`(execve/127/126 分岐)をパイプ子で再利用するため非 static 化するか複製するか、Norm(file 5 関数上限)とプロト公開のバランスで判定。
