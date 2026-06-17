# 01-design.md

- **Issue:** #6 cd / export / unset(可変 env 導入)
- **Stage:** 1/5 Architect

## 方針 (1行)
起動時に継承 envp を `t_shell.env` に複製して所有し、全 env 参照(env builtin / PATH探索 / execve / cd・export・unset)をこの可変コピー経由に一本化したうえで、cd(相対/絶対)・export(NAME=VALUE)・unset(NAME)を親プロセス実行の builtin として追加する。展開/クォート/cd拡張には一切触れない。

## 設計方針 (5-7行)
- **t_shell:** `char **env;` を追加(`envp` は撤去 or 残置だが参照は全て `env` に切替)。`shell_init` で `env = env_dup(envp)`(失敗時 init エラー)、`shell_cleanup` で `env_free(shell->env)`。所有権は shell が持ち、内部の set/unset は配列ごと再確保して付け替える。
- **データフロー:** `process_line → dispatch`(既存)に cd/export/unset を追加。各 builtin は `shell->env` を読み書きし、`run_external` の PATH探索/execve は `shell->env` を渡す(`find_command_path(shell->env, …)` に呼び出し側を更新、child_exec も `shell->env`)。
- **DB変更:** なし(該当しない)。
- **エラーハンドリング:** cd 失敗 = bash 風 `minishell: cd: <arg>: <strerror>` + status 1。export 不正識別子 = `minishell: export: \`<arg>': not a valid identifier` + status 1。env_set/dup の malloc 失敗は元配列を壊さず status 1 で抜ける(部分確保で壊れない不変条件を厳守)。
- **ファイル分割(Norm 5関数/file):** `src/env_utils.c`(env_dup/env_count/env_get/env_set/env_unset/env_free を 2ファイルに分割)・`src/builtin_cd.c`・`src/builtin_export.c`・`src/builtin_unset.c` を新設。`builtins.c` は dispatch 表に3エントリ追加(既存5関数枠を超えるため `is_builtin`/`run_builtin` の分岐追記のみ、各実体は新ファイル)。

## 採用理由とトレードオフ
- **採用:** NULL終端 `char **env` を「1要素拡張/詰めて再確保」で所有する案。理由: 既存 envp(`char **`)・execve 第3引数・env builtin と完全互換で、参照切替が最小、Norm内で書ける。
- **却下A:** リンクリスト/動的配列構造体で env を持つ案。理由: execve 用に毎回 `char **` へ変換が要り、env builtin/PATH探索の既存ループも全書き換え=スコープ膨張。
- **却下B:** `setenv/getenv/unsetenv` で OS の environ を直接操作する案。理由: 42 の許可関数外(`setenv/unsetenv` 不許可)で即アウト。`getenv` は許可だが書込手段が無く所有もできない。

## 1. 設計方針(t_shell / ライフサイクル / 参照切替 / ファイル分割)

### t_shell への追加と所有・ライフサイクル
- `include/minishell.h` の `t_shell` に `char **env;` を追加。既存 `envp` フィールドは **撤去**し全参照を `env` に置換(中途半端な二重持ちはバグ源)。
- `shell_init(shell, envp)`: `shell->env = env_dup(envp);` `if (!shell->env) return (1);`(main 側で perror + return 1)。`last_status = 0`。
- `shell_cleanup(shell)`: `env_free(shell->env); shell->env = NULL;`(+ 既存 `rl_clear_history`)。
- 所有権は **shell が唯一の所有者**。env_set/env_unset は新しい配列を作って `shell->env` を差し替え、旧配列を free する(呼び出し側で `shell->env = env_set(shell->env, …)` の形に統一)。

### 既存 envp 参照箇所の切替計画(3箇所)
| 箇所 | 現状 | 切替後 |
|---|---|---|
| `src/builtins.c` `bi_env` | `shell->envp[i]` を走査 | `shell->env[i]` |
| `src/execute.c` `run_external`/`child_exec` | `find_command_path(shell->envp, …)` / `execve(path, argv, shell->envp)` | `shell->env` を渡す |
| `src/path_utils.c` `find_command_path`→`find_path_env` | `char **envp` 引数 | シグネチャは `char **` のまま(呼び出し元が `shell->env` を渡すだけ。`find_path_env` も無変更) |
- `find_command_path` のプロトタイプ(`char **`)は不変。呼び出し元が渡す配列が複製コピーに変わるだけなので **再リンク影響は呼び出し側1行**。

### ファイル分割(42 Norm: 1ファイル5関数 / 関数25行)
| ファイル | 関数(目安) | 役割 |
|---|---|---|
| `src/env_utils.c` | `env_count` / `env_dup` / `env_get` / `env_free`(4) | 複製・参照・解放 |
| `src/env_set.c` | `env_key_len` / `env_match` / `env_set` / `env_unset`(4) | 追加・更新・削除(再確保) |
| `src/builtin_cd.c` | `bi_cd` / `cd_update_pwd`(2) | chdir + PWD/OLDPWD |
| `src/builtin_export.c` | `bi_export` / `is_valid_id` / `print_env_sorted_or_plain`(3) | NAME=VALUE 追加更新 + 単独表示 |
| `src/builtin_unset.c` | `bi_unset`(1) | NAME 削除 |
| `src/builtins.c`(既存) | `is_builtin`/`run_builtin` に3分岐追記 | ディスパッチ表拡張のみ |
- `builtins.c` は既に5関数(is_builtin/run_builtin/bi_pwd/bi_echo/bi_env)。`run_builtin` 内の分岐追記は関数数を増やさないので Norm 維持。cd/export/unset 実体は新ファイルへ。

## 2. 可変 env のデータ構造とヘルパ設計

`char **env`(NULL終端、各要素は `malloc` した `"KEY=VALUE"` 文字列)。

| ヘルパ | シグネチャ(案) | 所有権 / エラー時の状態 |
|---|---|---|
| `env_count` | `int env_count(char **env)` | 副作用なし。NULL終端まで数える。 |
| `env_dup` | `char **env_dup(char **src)` | 新規 `char **` + 各要素 `ft_strdup`。途中失敗時は確保済みを全 free し NULL を返す(部分配列を漏らさない)。**src は不変**。 |
| `env_get` | `char *env_get(char **env, const char *key)` | `key=` 接頭一致の **value 部へのポインタ(env 所有のまま)を返す**。コピーしない。無ければ NULL。cd の PWD 取得等で使用。 |
| `env_set` | `char **env_set(char **env, const char *key, const char *val)` | 同名あり=その要素を新 `"key=val"` に差し替え(旧要素 free、配列長不変)。同名なし=`count+2` で新配列確保しコピー+末尾追加+NULL終端、旧配列(外枠のみ)free。**失敗時は引数 env をそのまま返し変更なし**(=非破壊)。返り値を `shell->env` に必ず再代入。 |
| `env_unset` | `char **env_unset(char **env, const char *key)` | 該当要素を free し以降を1つ前へ詰め、末尾 NULL。配列は in-place 縮小(末尾要素 NULL 化)で再確保しなくても可。該当なしは無変更で同ポインタ返却。 |
| `env_free` | `void env_free(char **env)` | 既存 `free_argv` と同形(NULL ガード→各要素 free→外枠 free)。`free_argv` を流用してもよいが env 用に別名関数を置くと意図が明確。 |

### 部分確保で壊れない不変条件(必読・後段で裏取り)
- **env_set(同名なし時):** 新配列へ旧 `count` 要素を **ポインタコピー(shallow)** し、末尾に新 `"key=val"` を追加、その後 **旧配列の外枠だけ free**(要素は新配列へ移譲済みなので二重 free しない)。新 `"key=val"` の `malloc` が失敗したら新配列を free して旧 env を返す(要素移譲前に失敗判定する順序にする)。
- **env_set(同名あり時):** 先に新 `"key=val"` を確保 → 成功したら旧要素を free して差し替え。確保失敗なら何も触らず旧 env 返却(旧要素は無傷)。
- **env_dup:** 1要素でも `ft_strdup` 失敗したら、そこまで確保した要素+外枠を全 free して NULL。

## 3. cd の制御フロー(相対/絶対パスのみ)

```
bi_cd(shell, argv):
  if (!argv[1])            # 引数なし=HOME移動はスコープ外
      → エラー or no-op(下記方針)、status は据え置き/1
  oldpwd = getcwd(buf)     # 失敗してもエラーにせず後段で対処(bash 挙動準拠は最小)
  if (chdir(argv[1]) != 0):
      cmd_error("cd", argv[1], strerror(errno)) 相当
      shell->last_status = 1; return (1)
  cd_update_pwd(shell, oldpwd):
      newpwd = getcwd(buf)                       # 新カレント
      shell->env = env_set(shell->env, "OLDPWD", oldpwd)
      shell->env = env_set(shell->env, "PWD", newpwd)
  shell->last_status = 0; return (0)
```
- **引数なし `cd`:** スコープ外(HOME 移動しない)。最小方針 = 何もせず status 0 で返す or `cd: too few arguments` 相当。**推奨: 何もせず return(後続Issueで HOME 対応を差す)。** Implementer はどちらか1つに固定し DoD に明記。
- **getcwd 失敗時(削除済みディレクトリ等):** PWD 更新を best-effort にし、chdir 自体が成功していれば status 0。**ただし正確な PWD 整合は後段 Investigator が実コードで裏取り**(下記)。
- エラーメッセージは `execute.c` の `cmd_error` が 2引数(cmd, msg)。cd は `cd: <arg>: <msg>` の3要素なので、`cmd_error` を流用するなら `cmd="cd"`・`msg` に `<arg>: <strerror>` を連結 or 専用ヘルパを `builtin_cd.c` に置く(Norm 関数数に注意)。

## 4. export / unset の制御フロー

### export(オプションなし)
- **`export` 単独(argv[1]==NULL):** env を表示(最小可)。`export ` 接頭辞付き表示が bash 仕様だが、**最小では env builtin と同等の一覧出力で可**(DoD に「最小表示で可」と明記)。ソートは任意・スコープ外でよい。
- **`export NAME=VALUE`:** 最初の `=` の位置で分割。`key = argv[i][0..eq)`、`val = argv[i]+eq+1`。`is_valid_id(key)` 検証後 `shell->env = env_set(shell->env, key, val)`。同名は上書き。
- **`export NAME`(`=` なし=値なし):** bash は「キーだけ宣言(値は未設定)」。**最小方針: env_set で `"NAME="`(空値)を立てる or 何もしない。推奨 = 値なし export は何もしない(env に出さない)で簡略化し、後段で正式対応。** Implementer は1つに固定。
- **バリデーション `is_valid_id`:** 先頭が英字 or `_`、以降は英数 or `_`。不正なら `minishell: export: \`<arg>': not a valid identifier` を stderr、status 1(複数引数なら不正分のみエラーで他は処理)。`-n` や複数代入の網羅はスコープ外。

### unset(オプションなし)
- `unset NAME [NAME...]`:各 argv[i] に対し `is_valid_id` チェック(不正は bash 同様エラー status 1、最小では検証スキップでも可)→ `shell->env = env_unset(shell->env, argv[i])`。
- 該当なしは正常(status 0)。`=` を含む引数は bash ではエラーだが最小ではそのまま不一致で no-op で可。

### key 分割ヘルパ
- `=` の検索は `ft_strchr(arg, '=')`。NULL なら値なしケース。あれば `eq - arg` が key 長。key 部の一時確保(`ft_strndup`)→ env_set へ渡し、終わったら free。**この一時 key のリークに注意(下記メモリ方針)**。

## 5. execve に渡す env / PATH 探索の env 切替

- `run_external`(execute.c):`find_command_path(shell->env, argv[0])` に変更。child の `execve(path, argv, shell->env)` に変更(`child_exec` の第3引数 `envp` を呼び出し時に `shell->env` を渡す。`child_exec` のシグネチャ `char **envp` は不変=渡す中身が複製コピーに変わるだけ)。
- `find_command_path`/`find_path_env`(path_utils.c):**コード無変更**。引数で受けた `char **` を走査するだけなので、呼び出し元が `shell->env` を渡せば自動的に export/unset の結果(更新後 PATH)を見る。
- **export PATH=… → 外部コマンド解決に反映される**(同一 `shell->env` を参照するため)。これが「可変 env 一本化」の主目的。

## 6. メモリ管理方針

- **複製:** `env_dup` が起動時に全要素 deep copy。継承 envp は触らない(read only)。
- **set:** 同名更新=旧要素 free + 新要素差し替え(長さ不変)。新規追加=新外枠確保 → 旧要素 shallow 移譲 → 旧外枠のみ free(要素は二重 free しない)。`malloc` 失敗時は旧 env 無傷で返す。
- **unset:** 該当要素 free → 以降を前詰め → 末尾 NULL。外枠の縮小再確保は任意(しなくてもリークにならない=確保済みサイズは cleanup で一括 free)。
- **export の一時 key:** `ft_strndup` した key は env_set 呼出後に必ず free。エラー経路(不正 id で early return)でも free を通す。
- **cleanup:** `shell_cleanup` で `env_free(shell->env)` が全要素+外枠を解放。
- **子プロセス:** `child_exec` の execve 失敗経路は既存どおり `path`/`argv` を free して exit。**env(shell->env)は親所有なので子では free しない**(fork で複製された仮想メモリ上のコピー、exit で解放されるため明示 free 不要/二重管理しない)。
- **リーク観点(重点):** ① env_set 新規追加時の旧外枠 free 忘れ/要素二重 free、② export 一時 key の free 漏れ、③ env_dup 途中失敗時の部分配列漏れ、④ cd の getcwd バッファ(スタック配列なら問題なし、malloc 版なら free)。

## 7. スコープ / やらないこと / 後続Issue拡張ポイント

### スコープ(影響範囲・変更オーダー)
- **新規:** `src/env_utils.c` / `src/env_set.c` / `src/builtin_cd.c` / `src/builtin_export.c` / `src/builtin_unset.c`。
- **変更:** `include/minishell.h`(t_shell に `env`、プロトタイプ追記)、`src/main.c`(init/cleanup を env_dup/env_free に)、`src/builtins.c`(is_builtin/run_builtin に3分岐 + bi_env を `shell->env`)、`src/execute.c`(PATH探索/execve を `shell->env`)、`Makefile`(SRCS に新規5ファイルを明示追記)。
- **path_utils.c:** 無変更(呼び出し元のみ更新)。
- **想定行数:** minishell 側 250-350 行。1PRで完結する上限付近。膨らむ場合は「(A) 可変env導入 + env builtin切替 + execve/PATH切替」と「(B) cd/export/unset 追加」の2PRに分割可(Aが土台、Bが機能)。**本Issueは1PR想定で進めるが、Norm/行数超過時は A/B 分割を Reviewer に提案する。**

### やらないこと(3点)
1. **`$VAR` / `$?` 展開・クォート(`'` `"`)・リダイレクト・パイプ** — 別Issue。export した値が展開で見えるのは展開Issueの責務。本Issueは env への格納まで。
2. **cd 拡張(`cd` 引数なし=HOME / `cd ~` / `cd -`(OLDPWD移動)・`~user`)** — 別Issue。本Issueは相対/絶対パス1引数のみ。OLDPWD は **格納はするが `cd -` での利用はしない**。
3. **export の `-n`・複数代入の網羅的検証・値なし宣言の bash 完全準拠・export 単独のソート/`declare -x` 風表示** — 別Issue。最小の追加/更新/一覧に留める。

### 後続Issue拡張ポイント
- **展開:** `tokenize` 後段に `expand(shell->env, argv)` を挟む。env_get がそのまま値参照に使える(API 不変)。
- **クォート:** tokenize を quote-aware lexer に差し替え(本Issueの env API には影響なし)。
- **cd 拡張:** `bi_cd` に「引数なし→env_get("HOME")」「`-`→env_get("OLDPWD")+表示」の分岐を足すだけ。OLDPWD/PWD 更新ロジック(cd_update_pwd)は再利用。
- **export 完全版:** `is_valid_id` と `=` 分割は流用し、`-n`/値なし宣言/ソート表示を `builtin_export.c` に追加。

## 8. 完了条件(DoD)— Implementer / Reviewer 用チェックリスト
- [ ] `cd /tmp` 後 `pwd` が `/tmp`、`env | grep PWD` が `PWD=/tmp` に更新、`OLDPWD` に旧 PWD が入る。相対パス `cd ..` も反映。
- [ ] `cd <存在しないパス>` で `minishell: cd: ...: No such file or directory` 相当 + `last_status==1`、カレントは不変。
- [ ] `export A=1` 後 `env` に `A=1` が出る。`export A=2`(同名)で `A=2` に **上書き**(重複行が出ない)。
- [ ] `export PATH=...` 変更後、外部コマンド解決が新 PATH を見る(`shell->env` 一本化の確認)。
- [ ] `export ?bad=1` 等の不正識別子で `not a valid identifier` + `last_status==1`、env は不変。
- [ ] `unset A` で `env` から `A` が消える。存在しない名の unset は status 0 で no-op。
- [ ] 既存回帰:`ls`(外部 / 更新後 env で execve)・`pwd`・`echo -n` ・`env`・`exit` が従来どおり動く。
- [ ] **メモリリークなし**(leaks/valgrind 自作分ゼロ):env_dup / env_set 新規追加 / env_set 同名更新 / env_unset / export 一時 key / cleanup / 子 execve 失敗経路。
- [ ] env_set の malloc 失敗で旧 env が壊れない(非破壊)ことを意図したコードになっている。
- [ ] **42 Norm:** 関数25行以内・1ファイル5関数以内・while のみ(for/三項/switch なし)・グローバル0個・80列。
- [ ] `make` が `-Wall -Wextra -Werror` で警告なし。二度目の `make` で不要な再リンクが走らない。新規5ファイルは SRCS に明示追記済み。
- [ ] `process_line` / `run_external` / `run_builtin` の外部シグネチャが維持されている(env 参照切替は内部に閉じている)。

---

## 後続 Investigator への裏取り依頼(実コードで確認)
1. **env_set / env_unset の配列再確保リーク有無:** 新規追加時の「旧外枠 free + 要素 shallow 移譲」で二重 free / 漏れが起きないか、同名更新の旧要素 free 順序、`malloc` 失敗時の非破壊性を、実際の確保/解放対応で 1要素ずつ突き合わせて検証すること(leaks 実行 + コード追跡の両面)。
2. **cd 後の相対パス実行と PWD 整合:** `chdir` → `getcwd` → `env_set("PWD")` の順で、(a) PATH に `.` が無い環境で相対パス実行が `chdir` 後のカレントを基準にするか、(b) getcwd 失敗時(削除済みディレクトリ)の PWD/last_status の扱いが bash とどう乖離するか、(c) `env_get("PWD")` の値と実カレント(getcwd)がずれないかを、実コマンドを叩いて裏取りすること。
