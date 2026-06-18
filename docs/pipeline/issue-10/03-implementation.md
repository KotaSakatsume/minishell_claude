# 03-implementation.md

- **Issue:** #10 リダイレクト < > >> << (heredoc) + コマンド構造体
- **Stage:** 3/5 Implementer
- **Branch:** `feat/issue-10-redirect`(main = #2+#4+#6+#8 の上)
- **実行者:** パイプライン本体(サブエージェント側 Bash 拒否のためメインが直接実行)

## 作成 / 変更ファイル
| ファイル | 種別 | 役割 |
|---|---|---|
| `include/minishell.h` | 変更 | `t_tok_type` enum / `t_tok` / `t_redir` / `t_cmd` 定義、`t_lex` を argv→トークン列(head/tail)に、プロトタイプ刷新 |
| `src/lexer.c` | 変更 | argv 生成 → **型付きトークン列**生成(tok_push / finish_word(WORD emit) / lex_run / lex_tokens / lex_abort)|
| `src/lexer_op.c` | 新規 | `< > >> <<` 検出(is_op / op_len / op_type / emit_op)|
| `src/lexer_state.c` | 変更 | `handle_none` に演算子分岐(`is_op`→`emit_op`)。クォート内は無変更でリテラル |
| `src/parser.c` | 新規 | トークン列 → `t_cmd`(parse_tokens / p_word / p_redir / redir_push / parse_error)|
| `src/cmd.c` | 新規 | cmd_new / argv_push_cmd / free_cmd / free_redirs / free_tok |
| `src/redir.c` | 新規 | apply_redirs / apply_one / open_redir / restore_fds / redir_error(errno→msg)|
| `src/heredoc.c` | 新規 | run_heredocs / run_one_heredoc(pipe へ書込)/ is_delim |
| `src/process_line.c` | 変更 | t_cmd ベース exec_cmd(redir-only / builtin退避復元 / external)|
| `src/execute.c` | 変更 | `run_external(t_shell*, t_cmd*)` に変更、子で apply_redirs → execve |
| `src/tokenize.c` | 不変 | `free_argv` を `free_cmd` から流用 |
| `Makefile` | 変更 | SRCS に新規5ファイル追記 |

## 設計からの逸脱
- **なし(方針どおり)**。1点 Norm 対応: `op_type` の戻り型を `t_tok_type`→`int` に変更(norminette `MISALIGNED_FUNC_DECL` 回避。enum 値は int で互換)。
- 設計の固定判断どおり: heredoc は **pipe 方式 / 展開しない / add_history しない**、外部=子で dup2、ビルトイン=親で退避/復元、open 失敗 status 1 非実行、構文エラー status 2。

## 検証(すべてメイン Bash で実測 / Intel Mac, Apple clang 14, GNU Make 3.81)

### ビルド
- `make re` 警告/エラーなし、二度目 `make` 再リンクなし。

### 機能(非対話)
| 入力 | 結果 |
|---|---|
| `echo hi > f; cat < f` | `hi`(O_TRUNC + 入力)✅ |
| `echo a > f; echo b >> f; cat < f` | `a` / `b`(append)✅ |
| `pwd > f; cat < f; pwd` | ファイルに cwd + **端末にも cwd**(builtin 退避/復元)✅ |
| `cat << EOF`(EOF まで)| heredoc 内容を cat の stdin に ✅、history 非汚染 ✅ |
| `echo ">"` / `echo a">"b` | `>` / `a>b`(クォート内リテラル)✅ |
| `cat < nofile` | `No such file or directory` + `$?`=1、非実行 ✅ |
| `echo x > /tmp`(ディレクトリ)| `Is a directory` + 1 ✅(EISDIR 追加)|
| `ls >`(欠落)| `syntax error near unexpected token` + `$?`=2 ✅ |
| `echo x > a > b; cat < b` | `x`(最後が有効)✅ |
| `> f`(コマンド無し)| f を空作成、status 0 ✅ |
| 回帰 `ls`/`pwd`/`cd`/`export`/`unset`/`echo`/`'`/`"`/`$VAR`/`$?` | 従来どおり ✅ |

### Norm
- `norminette src include libft` → エラー種別 **`INVALID_HEADER` のみ(29ファイル)**。
- 自己監査: 新規各ファイル ≤5関数(lexer 5 / lexer_op 4 / parser 5 / cmd 5 / redir 5 / heredoc 3 / process_line 4 / execute 4)、全関数 ≤25行、while のみ、グローバル0個。

### リーク / fd リーク
- **メモリリーク:** 非フォーク経路(parser / redirect-only / builtin-redir / 構文エラー `ls >` / parse エラー `cat <` / quote+redirect / `> f`)を `leaks --atExit` → **`0 leaks for 0 total leaked bytes`** ✅。新設の t_tok / t_cmd / t_redir / argv の確保・解放を網羅。
  - 注: **外部コマンド経路(fork)は `leaks --atExit` が子の atexit で leak 解析を走らせハングする** macOS 既知挙動のため計測不可。子は `child_exec` で `path` を free し execve で置換、エラー時 `exit` で OS 回収。親は `free_cmd`→`free_redirs`(target free + heredoc fd close)で回収する設計を **code review で担保**。
- **fd リーク:** 複数 redir 後に外部 `ls /dev/fd` を実行 → `0,1,2,3,4`(**bash の `ls /dev/fd` と同一**)。redir 由来の残存 fd なし ✅。退避 fd・open fd・heredoc 読み端の対 close が効いている。

## Reviewer への申し送り
1. **【既知・受容】42 ヘッダー未付与** — `INVALID_HEADER` のみ。must 対象外。
2. **heredoc:** 展開しない(最小固定)/ add_history しない / 大容量(pipe バッファ ~64KB 超)はデッドロックしうる=スコープ外。
3. **fd 不変条件:** apply_one は dup2 後 open fd を close、heredoc は適用時 `r->fd=-1` で free_redirs の二重 close を防止。外部は子で dup2、親は free_redirs で heredoc 読み端 close。
4. **外部フォーク経路のリークは code review 担保**(leaks --atExit が fork で計測不可)。child_exec の path free / 親 free_cmd を重点確認。
5. **EISDIR**(`> directory`)= "Is a directory" を errno 分岐に追加済み。

## ブランチ / コミット
- ブランチ: `feat/issue-10-redirect`
- コミットハッシュ: `003b532` — "feat: redirections < > >> << (heredoc) with command struct"
