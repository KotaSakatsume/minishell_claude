# 03-implementation.md

- **Issue:** #8 クォート処理 + 変数展開($VAR / $?)
- **Stage:** 3/5 Implementer
- **Branch:** `feat/issue-8-quotes`(main = #2+#4+#6 の上)
- **実行者:** パイプライン本体(サブエージェント側 Bash 拒否のためメインが直接実行)

## 作成 / 変更ファイル
| ファイル | 種別 | 役割 |
|---|---|---|
| `src/strbuf.c` | 新規 | 自前可変長文字列 sb_init/sb_grow(static)/sb_push/sb_push_str/sb_release(5) |
| `src/lexer.c` | 新規 | tokenize / lex_run / finish_word / argv_push / lex_abort(static)(5) |
| `src/lexer_state.c` | 新規 | handle_char + is_space/handle_single/handle_double/handle_none(5) |
| `src/expand.c` | 新規 | var_name_len / append_status / expand_named(static) / expand_var(4) |
| `src/tokenize.c` | 変更 | 旧4関数を削除し **`free_argv` のみ残置**(レキサへ置換) |
| `src/process_line.c` | 変更 | `tokenize(line)` → `tokenize(shell, line)`(1行) |
| `include/minishell.h` | 変更 | `t_buf` / `t_lex` 定義、`tokenize` シグネチャ変更、新規プロトタイプ |
| `Makefile` | 変更 | SRCS に新規4ファイル追記、`tokenize.c` 残置 |
| 無変更 | — | `env_utils.c`(env_get 流用)/ builtins / execute / env_set |

## 設計からの逸脱
- **なし(機能・方針は設計どおり)**。ファイル名は `lexer_quote.c` → `lexer_state.c` に変更(状態分岐の責務名に合わせた)。expand.c は is_name_first/is_name_char を var_name_len に内包し4関数に収めた(Norm 余裕確保)。
- 設計の固定判断どおり: 未閉じクォート=エラー + `last_status=2` + 行破棄、`$1`=リテラル。

## 検証(すべて実測 / Intel Mac, Apple clang 14, GNU Make 3.81)

### ビルド
- `make re` 警告/エラーなし、二度目 `make` 再リンクなし。

### 機能(非対話)
| 入力 | 結果 |
|---|---|
| `echo '$HOME'` | `$HOME`(リテラル)✅ |
| `echo "$HOME"` / `echo $HOME` | HOME の値 ✅ |
| `echo [$NOPE]` | `[]`(未定義=空)✅ |
| `echo hi; echo $?; nosuchcmd; echo $?` | `hi` / `0` / `127` ✅ |
| `echo a"b c"d` | `ab cd`(連結 + クォート内空白)✅ |
| `echo a""b` | `ab`(空クォートは語を割らない)✅ |
| `echo '' END` | ` END`(空トークン1個 + END)✅ |
| `export X=hi; echo $X` | `hi`(#6 export 連携)✅ |
| `echo $ $1` | `$ $1`(リテラル `$` / `$1` は名前にならず)✅ |
| `echo "hello"` | `hello`(クォート除去)✅ |
| `echo "abc`(未閉じ) | `minishell: syntax error: unclosed quote` + `$?`=2、行破棄 ✅ |
| 回帰 `cd /tmp;pwd;export A=1;echo $A;unset A;echo [$A]` | `/private/tmp` / `1` / `[]` ✅ |

### Norm
- `norminette src include libft` → エラー種別 **`INVALID_HEADER` のみ(24ファイル)**。
- 自己監査: 新規各ファイル ≤5関数(strbuf 5 / lexer 5 / lexer_state 5 / expand 4)、全関数 ≤25行、while のみ、グローバル0個。

### リーク
- `MallocStackLogging=1 leaks --atExit`(入力: シングル/ダブルクォート、`$?`、長い変数の `$X$X$X`(grow 複数回)、`a"b c"d`、`''`、未閉じクォート)→ **`0 leaks for 0 total leaked bytes`** ✅
- 構築中 buf(release しなかった語は tokenize で `free(buf.data)`)、展開 name(env_get 後 free)、argv grow、未閉じエラー経路をカバー。

## Reviewer への申し送り
1. **【既知・受容】42 ヘッダー未付与** — `INVALID_HEADER` のみ。must 対象外。
2. **【既知差分】`$1`(数字始まり)はリテラル `$1`** — bash は位置パラメータ(空)。minishell は位置パラメータ非対応の必然。
3. **未閉じクォート = エラー + status 2 + 行破棄**(マルチライン継続は別Issue)。
4. **realloc 非使用** — strbuf は倍化 malloc + 手動コピー + free の自前 grow。
5. **展開のメモリ順序** — env_get の借用ポインタは sb_push_str でコピーしてから name を free(use-after-free なし)。
6. **フィールド分割なし**(`X="a b"; echo $X` は1引数のまま)は本Issueのスコープ外(設計どおり)。

## 段階4 差し戻し対応(round-trip 1)
- **must-1(Reviewer 検出):** unquoted の空展開(`$NOPE` 単独)が空引数を生成し `: Permission denied` を出していた(bash は no-op)。
- **修正:** `t_lex.had_quote` を追加。クォート開始で `had_quote=1`、`finish_word` で **`!had_quote && 空語` は捨てる**(クォート付き空 `""`/`"$NOPE"` は保持)。
- 再検証: `$NOPE`→no-op、`echo $NOPE`→argv `{echo}`、`echo "$NOPE"`→空引数保持、全回帰 + leaks 0 + Norm OK。

## ブランチ / コミット
- ブランチ: `feat/issue-8-quotes`
- 初回コミット: `e617922` — "feat: quote handling and variable expansion ($VAR / $?)"
- 差し戻し修正: 下記 fix コミット(had_quote による空語除去)
