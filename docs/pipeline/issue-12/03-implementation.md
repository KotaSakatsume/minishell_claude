# 03-implementation.md

- **Issue:** #12 パイプ |(複数コマンドの pipe/fork 接続)
- **Stage:** 3/5 Implementer
- **Branch:** `feat/issue-12-pipe`(main = #2+#4+#6+#8+#10 の上)
- **実行者:** パイプライン本体(サブエージェント側 Bash 拒否のためメインが直接実行)

## 作成 / 変更ファイル
| ファイル | 種別 | 役割 |
|---|---|---|
| `include/minishell.h` | 変更 | `t_cmd.next` / `TOK_PIPE` 追加、`exec_pipeline`/`pipe_child`/`parse_error`/`p_pipe`/`exec_external`/`wait_status` プロトタイプ公開 |
| `src/lexer_op.c` | 変更 | `|` を is_op/op_len(`|`=1で `||` 誤判定回避)/op_type(TOK_PIPE) |
| `src/parser.c` | 変更 | `parse_error` 非static化、`p_word` がトークンを進める形に、`parse_tokens` を `|` 分割対応(cur 持ち回り)|
| `src/parser_pipe.c` | 新規 | `p_pipe`(空コマンド/末尾 `|` の syntax error + cmd 連結)|
| `src/cmd.c` | 変更 | `cmd_new` で `next=NULL`、`free_cmd` をリスト全解放に |
| `src/pipeline.c` | 新規 | count_cmds / pipe_fork_one / wait_all / exec_pipeline(pipe/fork/fd持ち回り/全子waitpid)|
| `src/pipeline_child.c` | 新規 | pipe_dup / child_not_found / pipe_child(子の dup2 + heredoc/redir + builtin子実行/execve)|
| `src/execute.c` | 変更 | `child_exec`→`exec_external`(redir 分離・非static)、`wait_status` 非static、`child_run_external` 抽出 |
| `src/process_line.c` | 変更 | `cmd->next ? exec_pipeline : exec_cmd` 分岐 |
| `Makefile` | 変更 | SRCS に新規3ファイル追記 |

## 設計からの逸脱
- **`free_pipeline` を新設せず `free_cmd` をリスト対応に**(`next` を辿る while ループ)。機能同一でファイル/関数数を節約(cmd.c は5関数維持)。
- リダイレクト分離: `child_exec`(redir+execve)を **`exec_external`(execve のみ)** に分け、redir 適用を呼び出し側(`child_run_external` / `pipe_child`)に移動。単一コマンドとパイプ子で重複なく共有。
- それ以外は設計どおり: 単一コマンドは exec_cmd 温存、パイプは exec_pipeline、子builtin は run_builtin→exit、最終段 status、heredoc 各子先読み。

## 検証(すべてメイン Bash で実測 / Intel Mac, Apple clang 14, GNU Make 3.81)

### ビルド
- `make re` 警告/エラーなし、二度目 `make` 再リンクなし。

### 機能(非対話)
| 入力 | 結果 |
|---|---|
| `echo hello \| cat` | `hello` ✅ |
| `ls \| wc -l` | 数値(6)✅ |
| `echo abc \| cat \| cat`(3段)| `abc`(ハングなし)✅ |
| `printf ... \| grep root \| wc -l` | `1`(3連結)✅ |
| `echo hi \| cat > /tmp/po; cat < /tmp/po` | `hi`(**redir が pipe より優先**)✅ |
| `false \| true; $?` / `true \| false; $?` | `0` / `1`(**最終段**)✅ |
| `echo x \| cat`(パイプ内builtin)| `x` ✅ |
| `echo "\|"` | `\|`(クォート内リテラル)✅ |
| `\| cat` / `echo hi \|` / `echo a \| \| cat` | `syntax error` + `$?`=2 ✅ |
| **回帰** 単一builtin `cd /tmp;pwd` / `export REGR=yes;env\|grep` / `unset` | 親実行でシェルに反映 ✅ |
| **回帰** redirect/quote/`$`展開 | 不変 ✅ |

### Norm
- `norminette src include libft` → エラー種別 **`INVALID_HEADER` のみ(32ファイル)**。
- 自己監査: 新規/変更各ファイル ≤5関数(pipeline 4 / pipeline_child 3 / parser 5 / parser_pipe 1 / execute 5 / lexer_op 4 / cmd 5)、全関数 ≤25行(parse_tokens は p_word のトークン前進化で 24行に収めた)、while のみ、グローバル0個。

### リーク / fd リーク / ゾンビ / デッドロック
- **メモリリーク:** 非フォーク経路(パイプ list 構築 + parse error の free_cmd リスト解放。多段 `echo one | two | | four` 含む)を `leaks --atExit` → **`0 leaks`** ✅
- **fd リーク:** 多段パイプ後に外部 `ls /dev/fd` → `0,1,2,3,4`(**bash の `ls /dev/fd` と同一**)✅
- **デッドロック:** `echo x | cat | cat`(stdin 経由多段)でハングなし ✅
- **ゾンビ:** `wait_all` が全 pid を waitpid(最終段 status 採用)。プロトタイプ(Stage2)+実装で全子回収を確認 ✅
  - 注: **fork 子のメモリは `leaks --atExit` が子の atexit でハングするため自動計測不可**(#10 同様)。子は exit で OS 回収、親は `free_cmd` でリスト全解放、pids 配列も free。code review 担保。

## Reviewer への申し送り
1. **【既知・受容】42 ヘッダー未付与** — `INVALID_HEADER` のみ。must 対象外。
2. **fd 管理の核心:** 親=書端 fork 直後 close + 前段読端を `in_fd` 1変数で持ち回り、最終段で `*in_fd=-1`(post-loop の二重 close 回避)。子=`pipe_dup` で dup2 後に全 pipe fd close。
3. **パイプ内 builtin は子で実行 → exit**(cd/export/exit がシェルに効かない=bash 同等)。単一コマンドの builtin は **exec_cmd 温存**で親実行(cd 等がシェルに効く回帰維持)。
4. **redir/heredoc 優先:** 子で pipe dup2 → run_heredocs → apply_redirs の順。`echo x | cat > out` は out に書く。
5. **空コマンド境界:** `argv[0]==NULL && redirs==NULL` のみ syntax error(`> f | g` の redir-only 段は合法、bash 一致)。
6. **fork 子のリークは code review 担保**(leaks がハング)。pipe_child の exit 経路 / exec_external の path free を重点確認。
7. **pipe()/fork() 失敗の堅牢化は最小**(catastrophic 時は perror せず pid<0 を waitpid がスキップ)。改善余地は should。

## ブランチ / コミット
- ブランチ: `feat/issue-12-pipe`
- コミットハッシュ: `3f07f73` — "feat: pipe | (multi-command pipe/fork pipeline)"
