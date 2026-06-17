# 02-research.md

- **Issue:** #4 コマンド実行(トークン化 + PATH実行 + コアビルトイン)
- **Stage:** 2/5 Investigator
- **実測環境:** macOS (Darwin 21.6.0 / Intel), Apple clang 14, 2026-06-17 にパイプライン本体 Bash で実測

> Architect が裏取りを要請した2点(`<sys/wait.h>` マクロ可用性 / execve 後 errno→126・127 マッピング)を /tmp の実コードで検証した。

## 1. wait マクロと execve errno マッピング — 【実測済み】

`fork`/`execve`/`waitpid` + `<sys/wait.h>` マクロで小プログラムを書き、`-Wall -Wextra -Werror` でビルド・実行。

| 検証 | 入力 | 実測結果 |
|---|---|---|
| マクロ可用性 | `WIFEXITED/WEXITSTATUS/WIFSIGNALED/WTERMSIG` | ✅ Apple clang 14 + `-Werror` で警告なくビルド・動作 |
| 正常実行 | `/bin/ls -d /tmp` | `WIFEXITED` → code=0 |
| 存在しないパス | `/no/such/cmd` | execve errno=**2 ENOENT** → `exit(127)` → 親 code=**127** |
| 非実行ファイル | `/etc/hosts` | execve errno=**13 EACCES** → `exit(126)` → 親 code=**126** |
| ディレクトリ指定 | `/tmp` | execve errno=**13 EACCES**(macOS は `EISDIR` でなく `EACCES`)→ code=**126** |
| `getenv("PATH")` | — | set(存在する) |

### 確定した実装ルール(子プロセス内で判定)
```c
execve(path, argv, envp);          /* 戻った = 失敗 */
if (errno == ENOENT)               /* 2 */
    exit(127);                     /* command not found */
exit(126);                         /* EACCES 等(非実行/ディレクトリ含む) */
```
- **macOS 重要点:** ディレクトリの execve は `EISDIR` ではなく **`EACCES`** を返す。よって「`ENOENT`→127, それ以外→126」の単純2分岐で bash 互換の終了コードになる(設計 01-design.md:96 の `EISDIR` 分岐は無くても可。あっても害なし)。
- bash のメッセージは「`bash: /tmp: is a directory`」と区別するが、終了コード 126 は一致。本Issueの最小要件としてはコード一致を優先し、メッセージは `Permission denied` でも可(余裕があれば `errno==EISDIR||S_ISDIR` で "is a directory" に分岐。ただし `stat` 追加が要る → 最小では Permission denied に寄せてよい)。

## 2. PATH 解決の実装メモ(事実)

- `getenv("PATH")` は使用可(許可リスト)。ただし**子に渡す envp とは別物**になりうる(`getenv` はプロセス環境を見る)。設計は `find_path_env(shell->envp)` で **shell->envp から PATH= を自前で探す**方針(01-design.md:51)。export 別Issue を見据え、`getenv` でなく envp 走査に統一するのが一貫していて良い。
- PATH の各 dir に対し `dir + '/' + cmd` を組み立て `access(full, X_OK)` で最初のヒットを採用。`access` は許可リストにある。
- **PATH 未定義/空**のときは command not found(127)。`""`(空ディレクトリ要素)はカレントを意味するが、最小実装ではスキップでもよい(bash は空要素をカレントとして扱う。仕様外の深追いはしない)。
- `argv[0]` に `/` を含む場合(`ft_strchr`)は PATH 探索せず直接そのパスを execve(01-design.md:48)。実測の `/bin/ls` ケースで成立を確認。

## 3. 42 Norm 注意(本Issueで増える論点)

- 新規6ファイル+libft4関数。**1ファイル5関数以内**を守るため Architect の分割(tokenize/builtins/builtin_exit/execute/path_utils)に従う。builtins が5関数で埋まるので `bi_exit` を別ファイルにする判断は妥当。
- **三項演算子・for・switch 禁止** → errno 分岐や echo の `-n` 判定は `if/else` で。
- 関数25行以内 → `child_exec`(execve + errno分岐 + free + exit)は行数が膨らみやすい。free を `free_argv` 1コールに集約し25行に収める。
- **グローバル0個維持**。
- メモリ: `leaks` は fork する子は別プロセスなので親の leaks には出ない。**子プロセス側の execve 失敗経路のリーク**は `leaks` で親側に出ないが、Norm/評価では子側も全 free が要求される → コードレビューで担保(Reviewer 観点)。

## 4. メモリ・リーク確認の段取り(Implementer 向け)

- 親側: `printf 'pwd\nenv\necho hi\nls\n/no/such\n' | leaks --atExit -- ./minishell` で自作リーク0を確認(readline 由来は除外)。
- 子側 execve 失敗経路: コードで「execve 後に argv/path を全 free して exit」を目視保証(leaks では捕捉しづらい)。
- `exit` builtin はプロセスを即終了するので、その経路で確保中の argv が free されないと leaks が出る → `bi_exit` は exit 前に必要な解放を考慮(ただし exit 直前の argv は process_line の free_argv に到達しないため、exit builtin 経路のみ free してから exit するか、設計上「exit は即終了で OS が回収」を許容するか要判断。**bash 流儀では exit は即終了で OK、leaks 的には親プロセス終了で全回収**なので、`leaks --atExit` は exit 経由だと正常終了時の到達可能ブロックのみ報告)。

## 5. リスク箇所(上位3)

### リスク1: 子プロセスの execve 失敗経路でのリーク(評価直撃・leaksで見えにくい)
- 回避: `child_exec` で execve 後に `free_argv(argv)` + `free(path)` してから `exit(126/127)`。Reviewer は子経路を目視確認。

### リスク2: builtin を fork してしまい exit/cd が親に効かない
- 回避: 設計どおり **builtin は親で実行**(01-design.md:75,80)。`dispatch` で is_builtin 真なら fork しない。Implementer がうっかり全コマンド fork しないよう注意。

### リスク3: PATH 連結文字列の二重 free / 未 free
- 回避: `find_in_path` ループで `access` 後に都度 free、ヒット1本だけ呼び出し元へ返し、`child_exec` が execve 後/失敗後に free。所有権の移動を1箇所に明記。

## 6. 既存コードとの接続(事実確認)
- `src/process_line.c` は現状 `write + ft_strlen` でエコー。中身を tokenize→dispatch→free に差し替えるだけ。シグネチャ `int process_line(t_shell*, char *line)` 維持(設計 139行目)。
- `t_shell` は `envp`/`last_status` を既に持つ → 追加不要(01-design.md:36)。
- Makefile は SRCS 明示列挙。新規 .c を SRCS に、libft 追加関数を libft/Makefile の SRCS と libft.h に追記。

## Implementer が着手前/完了時に叩くコマンド
```bash
make                                  # 警告ゼロ
printf 'ls\n' | ./minishell           # 外部コマンド
printf '/bin/ls -d /tmp\n' | ./minishell  # 絶対パス
printf 'pwd\nenv\necho -n hi\necho hi\n' | ./minishell
printf 'nosuchcmd\n' | ./minishell    # not found(stderr + 内部127)
printf 'exit\n' | ./minishell         # 終了
printf 'pwd\nls\nnosuch\n' | leaks --atExit -- ./minishell  # 自作リーク0
/tmp/pdfvenv/bin/norminette src include libft   # ヘッダー以外クリーンか
```

## 申し送り
- 42 ヘッダーは Issue#2 同様**未付与**(ログイン確定待ち)。norminette は `INVALID_HEADER` を出すが既知保留。
- 終了コード内部値は正しく持つが `echo $?` での確認は `$?` 展開(別Issue)後。本Issueでは last_status の内部値を簡易手段(例: 一時的に exit コードで確認)で担保。

## 関連ファイル
- 設計入力: `docs/pipeline/issue-4/01-design.md`
- 本成果物: `docs/pipeline/issue-4/02-research.md`
