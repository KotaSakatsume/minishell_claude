# 02-research.md

- **Issue:** #10 リダイレクト < > >> << (heredoc) + コマンド構造体
- **Stage:** 2/5 Investigator
- **実測環境:** macOS (Darwin 21.6.0 / Intel), Apple clang 14, 2026-06-17 にパイプライン本体で実測
- **方式:** Architect 要請の5点をプロトタイプ実コードで裏取り。

## 1. O_TRUNC / O_APPEND の実挙動 — 【実測済み】
プロトタイプで `open(O_WRONLY|O_CREAT|O_TRUNC,0644)`→write→`O_APPEND`→write→`O_TRUNC`→write の順で検証:
- 最終内容 = `third`(O_TRUNC が既存内容を消去)✅
- 途中の O_APPEND は既存に追記(`first`→`first\nsecond`)を確認 ✅
- **確定フラグ:** TOK_OUT=`O_WRONLY|O_CREAT|O_TRUNC` / TOK_APPEND=`O_WRONLY|O_CREAT|O_APPEND` / mode `0644`。TOK_IN=`O_RDONLY`。

## 2. ビルトイン親実行の dup 退避/復元 — 【実測済み・端末復帰OK】
`saved=dup(1); dup2(fd,1); close(fd); printf(...); dup2(saved,1); close(saved)` を実行:
- リダイレクト中の出力は **ファイル b.txt に入り**、`dup2(saved,1)` 後の出力は **端末に戻った** ✅
- **確定:** ビルトインは親で `saved[0]=dup(0); saved[1]=dup(1)` 退避 → 各 redir を dup2 → builtin 実行 → `dup2(saved[i], std)` 復元 → `close(saved[0]); close(saved[1])`。各 open fd は dup2 後 close。これで `pwd > f; pwd` の2回目が端末に出る。
- **fd リーク防止:** 退避2本 + open fd を全 close すること(復元経路で漏らさない)。

## 3. open 失敗の errno → メッセージ — 【実測済み】
| 入力 | errno | メッセージ(bash 風) |
|---|---|---|
| `< nonexistent`(RDONLY) | **2 ENOENT** | `No such file or directory` |
| `> 権限なしファイル`(WRONLY) | **13 EACCES** | `Permission denied` |
| `> directory`(WRONLY) | **21 EISDIR** | `Is a directory` |
- **確定:** open 失敗後 errno 分岐: ENOENT→"No such file or directory"、EACCES→"Permission denied"、**EISDIR→"Is a directory"**(設計の2分岐に EISDIR を追加推奨)、その他→`strerror(errno)` 汎用。いずれも `minishell: <target>: <msg>` + `last_status=1` + 非実行。
- メッセージは既存 `cmd_error` と同じ write 連結スタイルで `redir_error(target, msg)` を新設。

## 4. heredoc の pipe 方式 — 【実測済み】
`pipe(p); write(p[1], "line1\nline2\n"); close(p[1]); dup2(p[0], STDIN); read(STDIN)` →
- stdin から `line1\nline2` を読めた ✅。許可関数(pipe/write/read/dup2/close)のみで完結。
- **確定フロー(run_heredoc):** `pipe(p)` → `while(1){ line=readline("> "); NULL(EOF)→break; delimiter一致→break; write(p[1],line)+'\n'; free(line); }` → `close(p[1])` → `redir->fd = p[0]`。適用時 `dup2(p[0], STDIN)` + close。**add_history 呼ばない。**
- ⚠️ **リスク(pipe バッファ上限):** heredoc 内容を**子の reader が動く前に親が全部 write** するため、内容が pipe バッファ(macOS 既定 ~16-64KB)を超えると **write がブロック → デッドロック**。通常の heredoc(数行)は問題なし。**大容量 heredoc は本Issueのスコープ外**(必要なら writer を fork する別Issue)。短い heredoc 前提を DoD/PR に明記。
- **複数 heredoc** `<< A << B`: 全て先読みし、redirs 出現順 dup2 で**最後が stdin に残る**。非最終の読み端は適用時/`free_redirs` で close(リーク防止)。

## 5. レキサ演算子境界(設計確認・実装時検証項目)
- 現状レキサは `< > >>` を通常文字として buf に push(演算子非対応)。本Issueで `handle_none` に検出を足す。
- **検証すべき挙動(Implementer 完了時):**
  - `>file`(空白なし)→ `>` トークン + WORD `file`(演算子が `finish_word` を呼んで語境界)
  - `echo ">"` → WORD `>`(クォート内は `handle_double` のまま=リテラル)
  - `a>b` → WORD `a` + `>` + WORD `b`
  - `>>`/`<<` の2文字判定(`op_len`)、`> >`(空白あり)は2つの `>` トークン
- `handle_single`/`handle_double` は**無変更**でクォート内リテラルが保たれる(#8 のコードがそのまま効く)。

## 6. 既存コードとの接続(事実確認)
- `src/lexer.c` の `t_lex` は現状 `argv(char**)` を持つ。これを `t_tok *head/*tail` に変更し、`finish_word` は WORD トークンを emit、新 `emit_op` が REDIR トークンを emit。
- `src/process_line.c` は `tokenize(shell,line)→dispatch→free_argv`。→ `lex_tokens→parse_tokens→exec_cmd(heredoc→apply_redirs→dispatch→restore)→free_cmd` に再構成。
- `src/lexer_state.c` `handle_none` に `<`/`>` 分岐追加。`handle_single/double`・`expand.c`・`strbuf.c` は無変更。
- `builtins.c`/`execute.c`/`env_*` は argv を受ける API 不変。execute は子で `apply_redirs` を足す。
- `tokenize.c` の `free_argv` は parser が生成する `cmd->argv` の解放に流用(`free_cmd` 内)。

## 7. 42 Norm 注意
- 新規5ファイル。各 ≤5関数(設計の関数割当に従う)。
- 連結リスト操作(tok/redir push、free)・apply_redirs のループは25行超過しやすい → `apply_one`/`open_redir`/`redir_push` 等に分割(設計どおり)。
- 三項禁止 → open フラグ選択や errno 分岐は if/else。while のみ。
- enum/struct はヘッダ定義(グローバル変数ではない=Norm のグローバル0個に抵触しない)。

## 8. リスク箇所(上位3)
### リスク1: fd リーク(退避 fd / open fd / heredoc 両端)
- 回避: 「open fd は dup2 後 close」「退避 fd は復元後 close」「heredoc 書き端は read 前 close・非最終読み端は close」を不変条件に。Implementer は redir 多用後に fd 残数を確認。

### リスク2: heredoc の pipe バッファ・デッドロック(大容量)
- 回避: 通常 heredoc 前提。大容量はスコープ外と明記。EOF(Ctrl-D で delimiter 未達)時は警告して読んだ分で続行、pipe を正しく閉じる。

### リスク3: str 移譲漏れ / エラー経路の tok・cmd 解放漏れ
- 回避: WORD→argv / REDIR 直後 WORD→target へ移譲後 `tok->str=NULL`。構文エラー early return で `free_cmd`+`free_tok` を必ず通す。`leaks` で構文エラー・open 失敗・heredoc 入力を確認。

## 9. Implementer が叩く検証コマンド
```bash
make
printf 'echo hi > f\ncat < f\n' | ./minishell           # hi
printf 'echo a >> g\necho b >> g\ncat < g\n' | ./minishell # a / b
printf 'pwd > p\ncat < p\npwd\n' | ./minishell            # builtin redir + 端末復帰
printf 'cat << EOF\nx\ny\nEOF\n' | ./minishell             # heredoc x/y
printf 'cat < nofile\necho $?\n' | ./minishell             # No such file, 1
printf 'ls >\necho $?\n' | ./minishell                     # syntax error, 2
printf 'echo x > a > b\ncat < b\n' | ./minishell           # b=x
printf 'echo ">"\necho a">"b\n' | ./minishell              # > / a>b
printf 'echo hi > f\ncat < f\n' | leaks --atExit -- ./minishell 2>&1 | grep 'leaks for'
/tmp/pdfvenv/bin/norminette src include libft
```

## 申し送り
1. **42 ヘッダー未付与**(ログイン待ち)継続。
2. **EISDIR(`> directory`)= "Is a directory"** を errno 分岐に追加(設計の ENOENT/EACCES に加える)。
3. **heredoc 展開なし**(設計固定)、**add_history しない**、**大容量 heredoc はスコープ外**。
4. **PR 規模**: 350-500行想定。ユーザー判断で heredoc 込み1PR。肥大時は Reviewer が分割提案可。

## 関連ファイル
- 設計入力: `docs/pipeline/issue-10/01-design.md`
- 本成果物: `docs/pipeline/issue-10/02-research.md`
