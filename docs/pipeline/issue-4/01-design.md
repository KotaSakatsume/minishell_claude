# 01-design.md

- **Issue:** #4 コマンド実行(最小)
- **Stage:** 1/5 Architect

## 方針 (1行)
`process_line()` のエコーを「空白split → builtin/external ディスパッチ」に差し替え、外部は `PATH探索→fork/execve/waitpid`、builtin(pwd/echo/env/exit)は親プロセスで直接実行し、終了コードを `t_shell.last_status` に保存する。クォート/展開/リダイレクト/パイプには一切触れない。

## 1. 設計方針 (ファイル分割 / データフロー / t_shell)

### データフロー
```
repl_loop
  └─ process_line(shell, line)            # 接続点。シグネチャ維持
       ├─ tokenize(line) -> char **argv    # 空白split
       ├─ argv==NULL or argv[0]==NULL -> 何もしない(last_status は据え置き)
       ├─ dispatch(shell, argv):
       │    ├─ is_builtin(argv[0]) -> run_builtin(shell, argv)  # 親で実行
       │    └─ else               -> run_external(shell, argv)  # fork/execve
       └─ free_argv(argv)                  # 親側の所有権をここで解放
```

### ファイル分割(42 Norm: 1ファイル5関数以内・関数25行以内)
| ファイル | 関数(目安) | 役割 |
|---|---|---|
| `src/process_line.c` | `process_line` / `dispatch`(2) | 接続点 + builtin/external 分岐 |
| `src/tokenize.c` | `tokenize` / `count_words` / `word_len` / `fill_argv` / `free_argv`(5) | 空白split と argv 解放 |
| `src/builtins.c` | `is_builtin` / `run_builtin` / `bi_pwd` / `bi_echo` / `bi_env`(5) | ディスパッチ表 + pwd/echo/env |
| `src/builtin_exit.c` | `bi_exit`(1) | exit(builtins.c が5関数で埋まるため分離) |
| `src/execute.c` | `run_external` / `resolve_path` / `child_exec` / `wait_status`(4) | PATH解決 + fork/execve/waitpid |
| `src/path_utils.c` | `find_in_path` / `path_join` / `find_path_env`(3) | `$PATH` 分割と `dir + "/" + cmd` 連結 |

> 注: builtin は引数を取らない最小仕様(echo のみ `-n`/引数表示あり)。run_builtin は関数ポインタ表でも if/else 連鎖でも可だが、Norm の関数25行に収めるため「`is_builtin` で判定 → `run_builtin` 内で `ft_strncmp` 連鎖から各 `bi_*` を呼ぶ」構成を推奨。

### t_shell への追加
**追加不要。** `char **envp`(env / PATH参照 / execve の第3引数)と `int last_status`(終了コード保存)で足りる。envp は起動時のものをそのまま読み書きせず参照する(export/unset は別Issueなので複製不要)。

## 2. トークン化の方針

- **実装:** 空白(スペース/タブ)区切りの自前 split。libft の `ft_split` 相当は **区切り文字が複数(' ' と '\t')** なので汎用 `ft_split(char, ...)` ではなく、minishell 側 `src/tokenize.c` に専用実装を置く(将来クォートを差し込む拡張点になるため libft に埋めない)。
- libft へ追加するのは split そのものではなく補助の `ft_strndup` / `ft_strncmp` / `ft_strdup` / `ft_strchr` 程度(builtin判定・word複製で必要)。`ft_split` 本体は libft に入れず minishell 側に持つ。
- **argv の所有権:** `tokenize` が `malloc` した NULL終端 `char **`(各要素も malloc複製)。**親プロセスでの解放責任は `process_line` 末尾の `free_argv(argv)` に一本化**。dispatch 内では free しない。
- 空入力(全空白)は `tokenize` が `argv[0]==NULL` の配列、または NULL を返す。どちらでも dispatch 前に「何もしない」で抜ける。`malloc` 失敗時は NULL を返し、`process_line` は `last_status` を据え置いて return(クラッシュさせない)。

## 3. PATH 解決と execve の制御フロー

### 経路分岐(resolve_path)
1. `argv[0]` が `/` を含む(`ft_strchr(argv[0], '/')`)→ **PATH探索しない**。そのパスを実行候補にする。
   - `access(path, X_OK)` 失敗時の errno で 126/127 を判定(下記)。
2. `/` を含まない → `find_in_path`:
   - `find_path_env(envp)` で `PATH=` を探す。**PATH 未定義/空なら command not found 扱い(127)**。
   - PATH を `:` で分割し各 `dir` に対し `path_join(dir, "/", cmd)` → `access(full, X_OK)` が成功した最初を返す。
   - どこにも無ければ NULL → 127。
   - 連結文字列は使い終わるたびに free(下記メモリ管理)。

### fork / 子 / 親
```
pid = fork();
pid == 0  (子): child_exec(path, argv, envp)
                  execve(path, argv, envp);
                  // execve が返った = 失敗。errno で 126/127 を判定し
                  // stderr へメッセージ + 確保物を全 free + exit(126 or 127)
pid > 0   (親): waitpid(pid, &status, 0);
                shell->last_status = wait_status(status);
fork 失敗: perror("minishell: fork") + last_status = 1
```

### 終了コード変換(wait_status)— マクロは使用可
- `WIFEXITED(status)` → `WEXITSTATUS(status)`
- `WIFSIGNALED(status)` → `128 + WTERMSIG(status)`(シグナル終了の bash 慣習。本Issueはシグナルハンドラを入れないが、子が外部要因で殺された場合の変換だけ正しく持つ)
- これらは `<sys/wait.h>` のマクロで、許可関数縛りに抵触しない(関数呼び出しではない)。三項演算子は Norm 禁止なので `if/else` で書く。

## 4. ビルトインのディスパッチ設計

- **分岐位置:** `dispatch()`。`is_builtin(argv[0])` が真なら `run_builtin`(**親プロセスで実行**。fork しない=exit やカレントディレクトリ変更が親に効く設計を後段 cd 用に温存)。偽なら `run_external`。
- **各 builtin の最小仕様(引数なし基本):**
  - `pwd` : `getcwd` で取得し `write` で1行出力。失敗時 perror + status=1。**引数は無視**。
  - `echo`: `argv[1]` が `-n` なら改行抑制。残り引数を空白区切りで出力(`-n` のみ本Issュの「最小」対象、`-nnn`連結等は別Issue)。status=0。
  - `env` : `shell->envp` を1行ずつ `write`。status=0。(`=` を含まない行のフィルタは export 実装後でよいので今は全行出力)
  - `exit`: 即 `exit()`。引数なし仕様なので `exit(shell->last_status)` 相当。**親プロセスを終了させる**(builtin を fork しない理由がここ)。
- 判定は `ft_strncmp` ではなく長さ込みの完全一致(`ft_strncmp(a,b,len+1)` で終端まで比較)で誤一致を防ぐ。

## 5. エラーハンドリングと終了コード

| ケース | last_status | 出力先/形式 |
|---|---|---|
| 正常終了 | 子の WEXITSTATUS | — |
| シグナルで子終了 | 128 + signo | — |
| command not found(PATHに無し/PATH未定義) | **127** | stderr: `minishell: cmd: command not found\n` |
| 実行権限なし(access X_OK 失敗 EACCES / ディレクトリ指定) | **126** | stderr: `minishell: cmd: Permission denied\n`(または `: is a directory`) |
| 空コマンド | **据え置き(変更しない)** | 何も出力しない |
| fork 失敗 | 1 | `perror("minishell: fork")` |
| getcwd 等 builtin 失敗 | 1 | perror |

- メッセージは `write(STDERR_FILENO, ...)` で出す(`printf` 系は使わず write 縛りに合わせる)。フォーマット組み立ては小ヘルパ `print_err(prefix, cmd, msg)` を `path_utils.c` か専用に置く(Norm の5関数枠に注意して配置)。
- 126/127 の判定は execve 直前の `access` 結果と execve 後の errno(`ENOENT`→127、`EACCES`/`EISDIR`/`ENOEXEC`→126)で行う。**判定は子プロセス内**(execve は子でしか呼ばないため)。

## 6. メモリ管理方針

- **argv:** `tokenize` が確保 → `process_line` 末尾 `free_argv` で必ず解放(成功/失敗/builtin/external いずれの経路でも通る)。
- **PATH連結文字列:** `find_in_path` のループ内で `path_join` の戻りを `access` 後に都度 free。ヒットした1本だけ呼び出し元へ返し、呼び出し元(`child_exec`)が execve 後/失敗後に free。
- **子プロセスでのリーク回避:** fork 後の子は親から argv・解決済み path を引き継ぐ。**execve 失敗時は argv 配列・path・PATH複製を全 free してから `exit()`**(子は親の free_argv に到達しないため子側で明示解放)。execve 成功時はプロセスが置き換わるのでリーク扱いにならない。
- readline 由来メモリは免除(Issue#2方針を継承)。leaks/valgrind で自作分ゼロを担保。

## 7. スコープ / やらないこと / 拡張ポイント

### スコープ(影響範囲・変更オーダー)
- **変更:** `src/process_line.c`(エコー→ディスパッチに差し替え)。
- **新規:** `src/tokenize.c` / `src/builtins.c` / `src/builtin_exit.c` / `src/execute.c` / `src/path_utils.c`、`include/minishell.h`(プロトタイプ追記)。
- **libft 追加:** `ft_strdup` / `ft_strndup` / `ft_strncmp` / `ft_strchr`(+ それぞれ libft/Makefile の SRCS と libft.h に明示追記)。
- **Makefile:** ルート SRCS に新規 .c を **明示追記**(ワイルドカード不使用、不要再リンク回避)。
- **想定行数:** minishell 側 250-350 行 + libft 60-100 行。1PRで完結する上限付近。大きければ「libft追加 + tokenize」を先行PRに割る選択肢あり(ただし本Issueでは1PR想定で進める)。

### やらないこと(3点)
1. **cd / export / unset、`$?`等の変数展開、クォート、ヒアドキュメント** — 別Issue。env は全行出力のままにし、export 実装時にフィルタを足す。
2. **リダイレクト(`> < >> <<`)・パイプ(`|`)・複数コマンド・`;`/`&&`** — 別Issue。今回は単一コマンド1本のみ。
3. **シグナル(ctrl-C/ctrl-\、sigaction、グローバル変数)・ワイルドカード** — 別Issue。子のシグナル終了コード変換(128+sig)だけは入れるが、ハンドラは入れない。

### 後続Issueへの拡張ポイント
- **クォート/展開:** `tokenize` を「split → quote-aware lexer」に差し替える1点に閉じる。`process_line` の構造(tokenize→dispatch→free)は不変。
- **リダイレクト/パイプ:** `dispatch` と `run_external` の間に `parse(argv) -> t_cmd` 段を挿入し、`run_external` を「fork前に dup2 で fd 付け替え」に拡張。`t_shell` に env 複製フィールドを足す形で吸収。
- **cd:** builtin を親実行にしてあるため `chdir` をそのまま追加でき、fork 設計の見直し不要。

## 8. 完了条件(DoD)

- [ ] `ls`(外部・PATH探索)が実行され、出力が出る。
- [ ] `/bin/ls`(絶対パス)・`./minishell` 風の相対パス(`/`含む)が PATH探索を経ずに execve される。
- [ ] `pwd` が `getcwd` 結果を1行出力する。
- [ ] `echo hello` が `hello\n`、`echo -n hello` が改行なしで出力される。
- [ ] `env` が `shell->envp` を1行ずつ出力する。
- [ ] `exit` でシェルが終了する(親プロセスが終わる)。終了コードは last_status。
- [ ] 存在しないコマンド → stderr に `minishell: <cmd>: command not found`、`last_status==127`。
- [ ] 実行権限のないファイル指定 → `last_status==126`。
- [ ] 空白のみ/空コマンドで何もせずクラッシュしない。last_status が壊れない。
- [ ] 子の終了コードが `last_status` に正しく入る(`echo $?` は別Issueだが、内部値が正しいこと)。
- [ ] **メモリリークなし**(leaks/valgrind、自作分ゼロ):argv・PATH連結・子プロセスの execve 失敗経路を含む。
- [ ] **42 Norm:** 関数25行以内・1ファイル5関数以内・while のみ(for/三項/switch なし)・グローバル0個・80列。
- [ ] `make` が `-Wall -Wextra -Werror` で警告なし。二度目の `make` で不要な再リンクが走らない。新規 .c は Makefile SRCS に明示追記済み。
- [ ] `process_line` のシグネチャ `int process_line(t_shell*, char *line)` が維持されている。
