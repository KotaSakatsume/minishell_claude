# 01-design.md

- **Issue:** #10 リダイレクト `<` `>` `>>` `<<`(heredoc) + コマンド構造体
- **Stage:** 1/5 Architect

## 方針 (1行)
既存 quote-aware レキサを「argv 直生成」から **型付きトークン列(WORD / REDIR_*)生成** に拡張し、新設パーサが連結リスト `t_redir` を組んだ `t_cmd { char **argv; t_redir *redirs; }` を作る。`process_line` は `t_cmd` を受け、**外部=子で dup2 / ビルトイン=親で標準fd退避 dup→dup2→実行→復元 close** の順でリダイレクトを適用する。heredoc は **pipe に書いて読み端を dup2**(一時ファイル不使用)、展開は **最小固定で「展開しない」**。

## 設計方針 (5-7行)
- **アーキテクチャ:** レキサは NONE 状態でのみメタ文字 `< > >> <<` を検出し、語を確定させてから演算子トークンを emit(クォート内はリテラル)。トークン列 `t_tok`(type + str の連結リスト)をパーサが走査し、WORD は argv へ、REDIR_* は直後 WORD を filename/delimiter として `t_redir` に積む。実行は `t_cmd` 1個(単一コマンドのみ)。
- **データフロー:** `process_line → tokenize_v2(=型付きトークン列) → parse(→t_cmd) → [heredoc 事前読込] → redir 適用 → dispatch(builtin/external) → 復元(builtin) → free(t_cmd/tok)`。
- **主要インターフェース:** `t_tok *lex_tokens(t_shell*, const char*)`、`t_cmd *parse_tokens(t_shell*, t_tok*)`、`int apply_redirs(t_cmd*, int *saved)`、`int restore_fds(int *saved)`、`int run_heredoc(t_shell*, t_redir*)`。
- **DB変更:** なし(該当しない)。
- **エラーハンドリング:** open 失敗(no such file / permission)= bash 風メッセージ + `last_status=1`・**コマンド非実行**。`>` の直後 WORD 欠落 = 構文エラー `syntax error near unexpected token` + `last_status=2`・非実行。fd 退避/open fd は全経路で確実に close。

## 採用理由とトレードオフ
- **採用:** 型付きトークン列 → パーサで argv/redir 振り分け。理由: redir は出現順・filename との対応・後続パイプ拡張(`t_cmd` 配列化)を考えると、argv に混ぜず「トークン型」で持つのが素直で、heredoc 事前読込やエラー位置検出も型で分岐できる。
- **却下A:** 「argv 生成は維持し redir だけ別収集(レキサが lx に redir リストを足す)」。理由: 速いが、レキサが構文(redir の後に語が来る規則)まで持ち責務肥大。`>` 直後欠落エラーや heredoc 順序をレキサ内で扱うと Norm 25行を圧迫し、パイプ拡張で破綻。
- **却下B:** heredoc を一時ファイル + unlink で実装。理由: 確実だが `/tmp` 競合・unlink 漏れ・予期せぬパス権限の懸念。pipe は許可関数(pipe/write/read/dup2/close)だけで完結し fd ライフサイクルが行内で閉じる。短い heredoc 前提で pipe バッファ上限(64KB)も実用上問題なし。

## 1. データフローとファイル分割(Norm 5関数/file・25行/func)

### パイプライン全体
```
process_line(shell,line)
  └ lex_tokens(shell,line)        -> t_tok* (型付きトークン連結リスト) | NULL
  └ parse_tokens(shell,tok)       -> t_cmd* | NULL(構文エラー時 last_status=2)
  └ exec_cmd(shell,cmd)
       ├ run_heredocs(shell,cmd)  : redirs 内の HEREDOC を事前に読み pipe fd を確保
       ├ apply_redirs(cmd,saved)  : builtin=親で退避→dup2 / external は子で実施
       ├ dispatch(shell,cmd->argv)
       └ restore_fds(saved)       : builtin のみ。external は子なので不要
  └ free_cmd(cmd) / free_tok(tok)
```
- 外部コマンド: **親では redir 適用しない**。fork 後の子で `apply_redirs`(=純粋 dup2 のみ、退避不要)→ execve。親は heredoc の読み端 fd と open 済みでない(子に渡す前に open する設計を下記で固定)に注意。**heredoc の pipe は親で作り読み端を子へ継承**。
- ビルトイン: 親で実行するため `saved[0]=dup(0); saved[1]=dup(1)` で退避 → dup2 → builtin → `dup2(saved,std)` で復元 → `close(saved)`。

### ファイル分割(目安・Implementer が Norm に寄せて再配分可)
| ファイル | 関数(各 ≤25 行) | 役割 |
|---|---|---|
| `src/lexer.c`(変更) | `tokenize` 廃止 or 改名 → `lex_tokens` / `lex_run` / `finish_word` / `tok_push` / `lex_abort` | 型付きトークン列生成へ変更 |
| `src/lexer_state.c`(変更) | `handle_char` / `handle_single` / `handle_double` / `handle_none` / `is_space` | NONE に `< > >>` 検出を追加 |
| `src/lexer_op.c`(新規) | `is_op_char` / `op_type` / `emit_op` / `op_len`(4) | 演算子検出・`>>`/`<<` 2文字判定・トークン emit |
| `src/parser.c`(新規) | `parse_tokens` / `take_word` / `take_redir` / `redir_push` / `parse_error`(5) | トークン列 → t_cmd、redir 収集、欠落エラー |
| `src/redir.c`(新規) | `apply_redirs` / `apply_one` / `open_redir` / `restore_fds` / `redir_error`(5) | open/dup2/退避/復元/エラー |
| `src/heredoc.c`(新規) | `run_heredoc` / `read_heredoc` / `hd_line` / `hd_pipe`(4) | delimiter 読込・pipe へ書込・読み端確保 |
| `src/cmd.c`(新規) | `free_cmd` / `free_redirs` / `free_tok` / `cmd_new`(4) | t_cmd / t_redir / t_tok の確保・解放 |
| `src/process_line.c`(変更) | `process_line` / `exec_cmd` / `dispatch` | t_cmd ベースへ再接続 |
| `src/tokenize.c`(変更) | `free_argv` のみ残置(既存互換) | argv 解放(parser が argv を作る経路で再利用) |
- `tokenize.c` の `free_argv` は **parser が argv(char**)を最終生成** するため引き続き使う(`free_cmd` 内で `free_argv(cmd->argv)`)。
- 上記は目安。5関数/file・25行を超えたら `lexer_op.c`↔`lexer_state.c`、`redir.c`↔`heredoc.c` 間で再配分する。

## 2. トークン型 / t_redir / t_cmd の定義案と所有権・free

```
typedef enum e_tok_type { TOK_WORD, TOK_IN, TOK_OUT, TOK_APPEND, TOK_HEREDOC } t_tok_type;

typedef struct s_tok {            /* レキサ出力(連結リスト) */
    t_tok_type      type;
    char            *str;         /* WORD は語、REDIR_* は NULL */
    struct s_tok    *next;
}   t_tok;

typedef struct s_redir {          /* パーサ出力(出現順の連結リスト) */
    t_tok_type      type;         /* TOK_IN/OUT/APPEND/HEREDOC */
    char            *target;      /* filename または heredoc delimiter */
    int             hd_fd;        /* HEREDOC: 事前読込した pipe 読み端。他は -1 */
    struct s_redir  *next;
}   t_redir;

typedef struct s_cmd {            /* 単一コマンド(パイプは後続で配列/リスト化) */
    char            **argv;       /* NULL 終端(既存 free_argv 互換) */
    t_redir         *redirs;
}   t_cmd;
```
- **所有権:** `t_tok.str` は語確定で `sb_release` の戻りを所有 → **パーサが argv へ移譲(WORD)** または **`t_redir.target` へ移譲(REDIR の直後 WORD)**。移譲後 `t_tok.str=NULL` にして二重 free を防ぐ。
- `t_cmd.argv` は NULL 終端 `char**`(WORD を順に詰める。`argv_push` 流用)。`free_cmd` → `free_argv(argv)` + `free_redirs(redirs)`。
- `free_redirs`: 各ノードの `target` を free、`hd_fd >= 0` なら **close**(heredoc 読み端のリーク防止)、ノード free。
- `free_tok`: 移譲されず残った `str`(エラー経路の未消費トークン)を free しつつリストを辿る。
- **連結リスト採用理由:** redir 個数は不定で配列は grow が必要。Norm 25行で grow を書くより list が単純。出現順 = 適用順なので追加は末尾(`redir_push` で末尾連結 or 逆順構築+反転は避け、末尾ポインタ保持)。

## 3. レキサのトークン化拡張(状態機械への組込み)

### NONE 状態への演算子検出追加(`handle_none`)
現 `handle_none` の分岐に **メタ文字 `<` / `>` を最優先で判定** する1分岐を足す:
1. 空白 → 既存どおり `finish_word`(語確定)。
2. **`<` または `>`(NONE のみ)→ `finish_word`(進行中の語を確定して語境界を作る)→ `emit_op`(演算子トークンを emit、`>>`/`<<` の2文字を `op_len` で判定し index を 1 or 2 進める)。**
3. `'` / `"` → 既存(クォート開始)。
4. `$` → 既存(展開)。
5. その他 → 既存(buf push)。
- **`>file`(空白なし)の語境界:** `>` を見た時点で `finish_word` を呼ぶため、直前に語があれば確定し、`>` 後の `file` は新しい WORD として開始する。**演算子自体が暗黙の語区切り** になる(bash 同等)。
- **`>>` / `<<` の2文字判定:** `op_len(s, i)` が `s[i]==s[i+1]` のとき 2(APPEND/HEREDOC)、そうでなければ 1(OUT/IN)を返す。`op_type` が type を決める。
- **クォート内はリテラル:** `handle_single` / `handle_double` は **変更しない**。`<`/`>` が SINGLE/DOUBLE 中に来ても既存どおり buf に push されるだけ。`echo ">"` は WORD `>` になる(演算子化されない)。
- **active な空語との整合:** `>` 直前が非 active(語が無い)なら `finish_word` は no-op(既存ガード `if(!lx->active)`)で安全に演算子だけ emit。

### emit_op とトークン構築の変更
- 現 `t_lex.argv(char**)` を **`t_tok *head` / `t_tok *tail`** に置換。`finish_word` は語を WORD トークンとして tail 連結、`emit_op` は REDIR トークン(str=NULL)を tail 連結。
- `tok_push(lx, type, str)`: ノード malloc → 末尾連結。失敗時は str を free して -1。
- 未閉じクォート(既存 `lex_abort`)・malloc 失敗時は **構築中トークンリストを `free_tok` で全解放** し NULL を返す(`last_status=2` は未閉じのみ)。

## 4. パーサ(トークン列 → t_cmd)

### `parse_tokens(shell, tok)`
- `cmd = cmd_new()`(argv=`{NULL}` の1要素、redirs=NULL)。
- トークン列を `while (tok)` で走査:
  - **WORD:** `argv_push(cmd, tok->str)`(str 移譲、`tok->str=NULL`)。
  - **REDIR_*(IN/OUT/APPEND/HEREDOC):** **次トークンが WORD でない(NULL=末尾、または REDIR)→ 構文エラー**(`parse_error`: `minishell: syntax error near unexpected token` 相当を stderr、`last_status=2`、`free_cmd`、NULL 返却)。WORD なら `redir_push(cmd, type, next->str)`(target 移譲、`next->str=NULL`)し **2トークン進める**。
- 全消費後 `cmd` を返す。`cmd->argv[0]==NULL` かつ redirs 有り(例: `> f` だけ)→ **空コマンド + リダイレクトのみ**: bash は `> f` で空ファイル作成し status 0。本Issueでも **open(=ファイル作成)は行い、argv 無しなら何も exec せず status 0**(下記7で固定)。
- **複数 redir:** `> a > b` は redirs に2ノード。**両方 open し最後が有効**(出現順 dup2 で後勝ち=stdout が b に残る)。`< f1 < f2` も両方 open、最後の読み端が stdin に残る(前の fd は dup2 で上書き前に close、下記5)。

### `>` 直後欠落エラーの例
| 入力 | 結果 |
|---|---|
| `ls >` | syntax error、status 2、非実行 |
| `> >` | 1個目 `>` の次が WORD でない(REDIR)→ syntax error、status 2 |
| `cat <` | syntax error、status 2 |

## 5. リダイレクト適用

### open フラグ / mode(`open_redir`)
| type | フラグ | mode |
|---|---|---|
| TOK_IN | `O_RDONLY` | — |
| TOK_OUT | `O_WRONLY | O_CREAT | O_TRUNC` | `0644` |
| TOK_APPEND | `O_WRONLY | O_CREAT | O_APPEND` | `0644` |
| TOK_HEREDOC | (open しない。事前読込済 `hd_fd` を使う) | — |

### 外部コマンド(子プロセスで dup2)
- fork 後の子で `apply_redirs(cmd, NULL)` を呼ぶ(saved=NULL=退避しない)。
- `apply_one`: type が IN/HEREDOC → `dup2(fd, STDIN_FILENO)`、OUT/APPEND → `dup2(fd, STDOUT_FILENO)`。dup2 後 **元の open fd / hd_fd を close**(複製済みなので不要)。
- redirs を **出現順** に `apply_one`。複数同方向は dup2 で後勝ち、各 open fd は適用ごとに close するため fd リークなし。
- open 失敗時は **子の中で** redir_error 表示 → `exit(1)`(execve 前)。**親側で open 失敗を先に検出する設計に統一**(下記推奨)。

### 推奨: open は親で先行実施し、失敗ならコマンド非実行
- 子の中で open すると「open 失敗時に status 1 で非実行」を親へ伝えるのに exit code 経由となり、外部とビルトインで経路が割れる。**統一のため、外部でも親で `apply_redirs` を先に走らせる方式は採れない(親の fd を汚す)** ため、本設計は:
  - **ビルトイン:** 親で退避 → open+dup2 → builtin → 復元。open 失敗で status 1、builtin 非実行、退避 fd を復元 close。
  - **外部:** **親で open のみ先行**して fd を `t_redir.hd_fd`/別フィールドに保持し、open 失敗なら fork せず status 1。成功なら fork → 子で dup2(open はしない、親が開いた fd を継承)→ 親は子の fd を待った後 close。
  - → 簡潔化のため **t_redir に `open_fd`(open 済み fd、未 open は -1)を持たせ、heredoc の `hd_fd` と統合**(1フィールド `fd` に集約)。`run_heredocs` と「外部の親 open」が同じ `fd` を埋める。子は `fd` を dup2 するだけ。

### ビルトイン(親で退避/復元)具体フロー
```
saved[0] = dup(STDIN_FILENO); saved[1] = dup(STDOUT_FILENO);   /* -1 なら error */
for each redir (出現順):
    fd = redir->fd  (heredoc/外部open 経由 or ここで open_redir)
    if fd < 0 -> restore + close(saved) + status 1, 非実行
    dup2(fd, target_std); close(fd)
run_builtin(shell, cmd->argv)
dup2(saved[0],0); dup2(saved[1],1); close(saved[0]); close(saved[1]);
```
- **退避 fd は必ず復元後 close**(2本)。dup2 後の open fd も close。これが fd リーク防止の中核。

## 6. heredoc 設計(pipe 方式・展開しない・add_history しない)

### `run_heredoc(shell, redir)`(parser 後・exec 前に全 heredoc を先読み)
1. `pipe(p)`。
2. `while (1)`: `line = readline("> ")`。`line==NULL`(EOF/Ctrl-D)→ 警告出して終了。`ft_strncmp(line, delimiter)` 一致 → `free(line)` して break。**`add_history` は呼ばない。**
3. 各行を `write(p[1], line, len)` + 改行。`free(line)`。
4. `close(p[1])`。`redir->fd = p[0]`(読み端を保持)。後で IN と同様 `dup2(p[0], STDIN)`。
- **複数 heredoc** `<< A << B`: bash は両方読むが **最後の heredoc が stdin に残る**。本設計は全 heredoc を read(先読み)し、redirs 出現順 dup2 で後勝ち。先読みした使われない pipe 読み端も `free_redirs`/適用時 close でリークさせない。

### 展開方針(最小固定)
- **本Issueは「heredoc 内の変数展開を行わない」に固定。** delimiter のクォート有無に関わらず行をそのまま流す。
- 理由: bash 準拠(未クォート delimiter なら展開)は expand を heredoc 行へ再適用する別経路が必要で Norm/スコープを膨らませる。`<<` の最小要件は「delimiter まで読み stdin へ流す」であり展開は後続Issueに切る。**この一文をImplementerへの確定指示とする。**

### リソース解放
- pipe 読み端 `redir->fd` は **適用時(dup2 後)close**、適用されなかった(複数 heredoc の非最終)読み端は `free_redirs` で `fd>=0` を close。書き端は read 終了で即 close。readline の各行は逐次 free。

## 7. エラー / 終了コード / 実行可否

| 事象 | メッセージ(stderr, bash 風) | status | 実行 |
|---|---|---|---|
| open 失敗(ENOENT) | `minishell: <file>: No such file or directory` | 1 | 非実行 |
| open 失敗(EACCES) | `minishell: <file>: Permission denied` | 1 | 非実行 |
| `>` 直後 WORD 欠落 | `minishell: syntax error near unexpected token` | 2 | 非実行 |
| heredoc EOF(delimiter 未達) | `minishell: warning: here-document ... EOF` 相当 | 0(bash は警告のみ) | heredoc は読んだ分で続行 |
| redir のみ(argv 空) `> f` | — | 0 | exec せずファイル作成のみ |
- **errno → メッセージ:** `open` 失敗後 `errno` を見て ENOENT/EACCES を分岐(既存 `cmd_error` と同じ write 連結スタイル)。その他 errno は汎用メッセージ + status 1。
- **適用順と中断:** redirs を順に open/適用し、**途中で open 失敗したら以降を中止しコマンド非実行**(bash 挙動)。既に dup2 済み/open 済みの fd は close(外部は子の exit、builtin は復元経路)。

## 8. メモリ / fd 管理(不変条件)

- **fd 不変条件:**
  - open した fd・heredoc pipe の両端・退避 dup fd は **必ず対になる close を持つ**(dup2 後の元 fd、復元後の退避 fd、適用されない heredoc 読み端)。
  - 外部: 子は dup2 後に元 fd を close、親は子へ継承した open fd を waitpid 後 close。
  - ビルトイン: 退避 2本 + 各 open fd を復元時に close。
  - エラー経路(open 失敗・構文エラー)でも、それまでに開いた fd を `free_redirs`(fd>=0 close)で回収。
- **メモリ不変条件:**
  - `t_tok.str` は WORD→argv / REDIR 直後 WORD→target へ **移譲後 NULL 化**(二重 free 防止)。
  - `free_cmd` = `free_argv(argv)` + `free_redirs`(target free + fd close + node free)。
  - `free_tok` = 残存 str free + node free(構文エラーで未消費トークンが残る経路を必ず通す)。
  - readline の各行は heredoc 内で逐次 free、delimiter 一致行も free。
- **リーク重点:** ① 移譲漏れ str、② heredoc 非最終読み端の close 漏れ、③ 退避 fd 復元後の close 漏れ、④ 構文エラー early return での tok/cmd 解放、⑤ open 成功後に後続 redir で構文/open エラー時の既存 fd 回収。

## 9. スコープ / やらないこと / 後続

### スコープ(影響範囲・変更オーダー)
- **新規:** `src/lexer_op.c` / `src/parser.c` / `src/redir.c` / `src/heredoc.c` / `src/cmd.c`。`include/minishell.h` に enum/`t_tok`/`t_redir`/`t_cmd` + プロトタイプ追記。
- **変更:** `src/lexer.c`(argv 生成 → トークン列生成)、`src/lexer_state.c`(`handle_none` に演算子分岐)、`src/process_line.c`(t_cmd ベース exec_cmd へ)、`src/tokenize.c`(`free_argv` 残置)、`Makefile`(SRCS に新規5ファイル追記)、`include/minishell.h`(`t_lex` を argv→tok head/tail に変更、`tokenize`→`lex_tokens`)。
- **無変更:** `expand.c` / `strbuf.c`(語構築は不変)、`builtins.c` / `execute.c` / `env_*`(argv を受ける API 不変。execute は子 dup2 を足す程度)。
- **想定行数:** minishell 側 350-500 行。やや大きめ。**1PR 維持可だが、肥大化したら (A) `< > >>` + parser + 外部/builtin 適用、(B) `<<` heredoc を後続PRに分割可**。ただし A 単体でも redir 構造体・適用経路は完結し DoD を満たすため、**heredoc を別PRに切る分割を第一候補として提案**(本ファイルでは A+B を1設計として記述、PR 分割判断は Implementer 着手時の行数で決定)。

### やらないこと(3点)
1. **パイプ `|`** ・ `2>` / `&>` 等 fd 番号指定・`;` / `&&` / `||` ・ワイルドカード ・シグナル(heredoc 中の Ctrl-C 等)— 別Issue。
2. **heredoc 内の変数展開の bash 完全準拠**(未クォート delimiter で展開)— 本Issueは「展開しない」に固定(6節)。`$VAR` を含む heredoc は素のまま流す。
3. **複数コマンド(`t_cmd` の配列/リスト化)** — 本Issueは単一 `t_cmd` のみ。後続で `t_cmd *next` を足してパイプ接続。

### 後続拡張ポイント(パイプ接続)
- `t_cmd` に `struct s_cmd *next` を追加 → `parse_tokens` が `|` トークンで `t_cmd` を切り、リスト化。
- 実行は `t_cmd` ごとに `pipe()` で前後を接続し fork、各子で `apply_redirs`(redir は pipe より後勝ちで上書き=bash 同等)。本設計の `apply_redirs`/`run_heredocs`/`free_cmd` はそのまま再利用でき、`exec_cmd` がループ化するのみ。
- `lexer_op.c` の `op_type` に `|` ケースを足すだけでレキサは拡張完了。

## 10. 完了条件(DoD)— Implementer / Reviewer 用チェックリスト
- [ ] `echo hi > f` でファイル f に `hi`(O_TRUNC、既存内容消去)。`cat < f` で `hi`。
- [ ] `echo a >> f` で f に追記(O_APPEND、既存保持)。
- [ ] `cat << EOF`(行入力 → `EOF` で終了)で入力行が `cat` の stdin に流れる。delimiter 行は出力されない。**history に積まれない**。
- [ ] **ビルトインにも適用:** `pwd > f` で f に cwd。`export X=1 > f`(builtin でも redir 解釈)。実行後 **シェルの標準出力が汚れず元に戻る**(`pwd > f; pwd` の2回目が端末に出る)。
- [ ] `echo ">"`(クォート内)→ リテラル `>` を出力(演算子化されない)。`echo a">"b` → `a>b`。
- [ ] **open 失敗 status 1 非実行:** `cat < nonexistent` → `No such file or directory`、`$?`=1、cat 実行されない。書込権限なしディレクトリ/ファイルへ `> /noperm` → `Permission denied`、status 1。
- [ ] **構文エラー status 2:** `ls >` → syntax error、`$?`=2、非実行。`> >`、`cat <` も同様。
- [ ] **複数 redir:** `echo x > a > b` → b に `x`(a は空作成)。`cat < f1 < f2` → f2 が有効。
- [ ] `> f`(コマンド無し)→ f を空作成、status 0。
- [ ] **既存回帰:** `ls` / `pwd` / `cd /tmp` / `export A=1` / `unset A` / `env` / `echo` / クォート(`'`/`"`)/ `$VAR` / `$?` 展開 が従来どおり(redir を含まない入力で挙動不変)。
- [ ] **メモリリークなし**(leaks/valgrind): t_tok / t_cmd / t_redir、heredoc readline 各行、移譲 str、エラー経路(構文 / open 失敗 / heredoc EOF)。
- [ ] **fd リークなし:** redir 多用後に `/dev/fd`(or lsof)で残存 fd 無し。退避 fd・open fd・heredoc pipe 両端が全 close。
- [ ] **42 Norm:** 25行/func・5関数/file・while のみ(for/三項/switch 無し)・グローバル0個・80列。
- [ ] `make` が `-Wall -Wextra -Werror` 警告なし。**二度目の `make` で不要な再リンク無し**(新規5ファイル SRCS 明示、`tokenize.c` 残置)。
- [ ] `process_line` / `dispatch` / `run_external` / `run_builtin` / `free_argv` の外部契約維持(argv 所有権・free 経路。変更は t_cmd 導入と内部のみ)。

---

## 後続 Investigator への裏取り依頼(実コードで確認・コードは設計のみ)
1. **heredoc pipe vs 一時ファイルの実現性と fd リーク:** `pipe()` に書いて読み端を `dup2(p[0], STDIN)` する方式で、(a) 許可関数のみで完結するか、(b) 短い heredoc で pipe バッファ(~64KB)上限に当たらないか、(c) 親で先読み→子へ読み端継承する際に書き端 `p[1]` の close 漏れが SIGPIPE/ブロックを起こさないか、(d) 複数 heredoc の非最終読み端 close 漏れが無いか、を実コード + `leaks`/fd 数確認で裏取り。pipe が困難なら一時ファイル+unlink へ切替判断。
2. **ビルトイン親実行での dup 退避/復元:** `saved=dup(1); dup2(fd,1); pwd; dup2(saved,1); close(saved); close(fd)` で **実際にシェルの標準出力が汚れず端末へ戻る**か(`pwd > f; pwd` の2回目が端末に出るか)、退避 fd の close 漏れが無いかを実機 + fd 数で確認。
3. **`>>` / `>` の O_APPEND / O_TRUNC 実挙動:** `O_WRONLY|O_CREAT|O_TRUNC,0644` で既存内容が消えるか、`O_APPEND` で追記されるか、新規作成時の mode が umask 適用後 `0644`(rw-r--r-- 相当)になるかを実ファイルで確認。
4. **open 失敗時の errno → メッセージ:** 存在しないファイルへの `< nofile` で `errno==ENOENT`、権限なしへの `> /file` で `errno==EACCES` になるか、ディレクトリへの `>` 等他 errno の扱いを実コードで確認し、bash の文言(`No such file or directory` / `Permission denied`)と status 1 を突き合わせる。
5. **レキサ演算子境界:** `>file`(空白なし)が `finish_word` 経由で `>` トークン + WORD `file` に割れるか、`echo ">"` がリテラルになるか、`a>b` が `a` `>` `b` になるか、既存クォート/`$`展開と干渉しないかを実コードで確認。
