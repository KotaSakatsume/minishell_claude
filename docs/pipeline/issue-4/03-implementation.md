# 03-implementation.md

- **Issue:** #4 コマンド実行(トークン化 + PATH実行 + コアビルトイン)
- **Stage:** 3/5 Implementer
- **Branch:** `feat/issue-4-execution`(`feat/issue-2-foundation` の上に構築)
- **実行者:** パイプライン本体(サブエージェント側 Bash 拒否のためメインが直接実行)

## 作成 / 変更ファイル
| ファイル | 種別 | 役割 |
|---|---|---|
| `src/process_line.c` | 変更 | エコー → `tokenize → dispatch → free_argv`。`dispatch` で builtin/external 分岐 |
| `src/tokenize.c` | 新規 | 空白split(is_space/count_words/fill_argv/tokenize/free_argv) |
| `src/builtins.c` | 新規 | is_builtin / run_builtin / bi_pwd / bi_echo / bi_env |
| `src/builtin_exit.c` | 新規 | bi_exit(親終了。exit 前に argv free) |
| `src/execute.c` | 新規 | run_external / child_exec / wait_status / cmd_error |
| `src/path_utils.c` | 新規 | find_command_path / find_path_env / path_join / search_path |
| `include/minishell.h` | 変更 | 新プロトタイプ追加(t_shell は変更なし) |
| `libft/{ft_strdup,ft_strndup,ft_strncmp,ft_strchr}.c` + `libft.h` + `Makefile` | 新規/変更 | builtin判定・word複製・PATH探索用 |
| `Makefile` | 変更 | SRCS に新規5 .c を明示追記 |

## 設計からの逸脱
- **なし。** 01-design / 02-research の方針どおり。
  - builtin は親実行(fork しない)→ exit/将来 cd を親に効かせる
  - errno→終了コードは実測どおり「ENOENT→127, else→126」(macOS はディレクトリも EACCES)
  - PATH は `getenv` でなく `shell->envp` 走査
  - ft_split は libft でなく minishell 側 tokenize に実装

## 検証(すべてメイン Bash で実測 / Intel Mac, Apple clang 14, GNU Make 3.81)

### ビルド
- `make re` → 警告/エラーなしで成功
- `make`(二度目) → `Nothing to be done for 'all'.`(再リンクなし)

### 機能(非対話 `printf ... | ./minishell`)
| コマンド | 結果 |
|---|---|
| `ls` | カレントのファイル一覧を出力 ✅ |
| `/bin/ls -d /tmp`(絶対パス) | `/tmp`(PATH探索を経ず execve)✅ |
| `pwd` | `/Users/.../minishell_claude` ✅ |
| `echo hello` / `echo -n hi` / `echo` | `hello\n` / `hi`(改行なし) / 空行 ✅ |
| `env` | envp を1行ずつ(PATH= / HOME= 等)✅ |
| `nosuchcmd` | stderr `minishell: nosuchcmd: command not found` ✅ |
| `exit` | `exit` 出力して終了 ✅ |
| 空白/空行のみ | 何もせずクラッシュなし ✅ |

### 終了コード(exit builtin が last_status を伝播する性質で確認)
- `ls; exit` → **0** ✅
- `nosuchcmd; exit` → **127** ✅
- `/etc/hosts`(非実行ファイル); exit → **126** ✅
- `/tmp`(ディレクトリ); exit → **126** ✅

### Norm
- `/tmp/pdfvenv/bin/norminette src include libft` → エラー種別は **`INVALID_HEADER` のみ(15ファイル)**。関数25行 / 5関数/ファイル / while のみ / グローバル0個 / 80列 はすべてパス。

### リーク
- `MallocStackLogging=1 leaks --atExit -- ./minishell`(入力 `pwd / echo hi / env / nosuchcmd`)→ **`Process NNNN: 0 leaks for 0 total leaked bytes.`** ✅
- 親プロセスはリークなし(not-found=127 経路含む)。fork-exec の子は execve 成功時はイメージ置換でリーク扱いにならず、失敗時は `child_exec` が `free(path)+free_argv(argv)` してから exit。argv は `process_line` 末尾 or `bi_exit` で free、path は `run_external` で free。
- 補足: `ls` 等を含む入力では readline の `leaks` 計装が子プロセスを保持して報告が遅延/ハングするため、親リーク確認は builtins + not-found 経路で実施(子経路は code review で担保)。

## Reviewer への申し送り
1. **【既知・受容】42 ヘッダー未付与** — norminette `INVALID_HEADER` のみ。ユーザー判断で保留(ログイン確定待ち)。must 差し戻し対象外。
2. **子プロセス execve 失敗経路の free** — `child_exec` で execve 後に `free(path) + free_argv(argv)` してから exit(126/127)。leaks では親側に出ないため目視確認推奨(リスク1)。
3. **builtin 親実行** — `dispatch` で is_builtin 真なら fork しない(リスク2)。
4. **PATH 連結の所有権** — `search_path` ループで access 後に都度 free、ヒット1本のみ呼び出し元へ。`run_external` が waitpid 後に free(リスク3)。
5. **echo は最小仕様** — `-n` 単独のみ対応(`-nnn` 連結や複数 `-n` は別Issue)。`env` は `=` フィルタなし(export Issue で対応)。

## ブランチ / コミット
- ブランチ: `feat/issue-4-execution`
- コミットハッシュ: `4767b2c` — "feat: command execution — tokenize + PATH exec + core builtins"
