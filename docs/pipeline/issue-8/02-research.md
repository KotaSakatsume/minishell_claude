# 02-research.md

- **Issue:** #8 クォート処理 + 変数展開($VAR / $?)
- **Stage:** 2/5 Investigator
- **実測環境:** macOS (Darwin 21.6.0 / Intel), Apple clang 14, 2026-06-17 にパイプライン本体で実測
- **方式:** Architect 要請の3点(grow vs 2パス / `$`名前境界 / 空クォート bash 挙動)をプロトタイプ実コード + bash 参照で裏取り。

## 1. 可変長バッファ: 自前 grow を採用で確定 — 【実測済み・リーク0】

プロトタイプで `t_buf`(data/len/cap)+ sb_init/sb_grow/sb_push/sb_push_str/sb_release を実装し `-Wall -Wextra -Werror` でビルド・実行・leaks。

| 検証 | 結果 |
|---|---|
| 1000文字 push(cap 16 から倍化複数回)+ "END" 連結 | `len=1003`、末尾 `xxEND` ✅ 正しく grow |
| `sb_grow`(新cap=cap*2, malloc+手動コピー+free) | **5〜7行**(Norm 25行に余裕)✅ |
| `leaks --atExit` | **`0 leaks for 0 total leaked bytes`** ✅ |

**確定:**
- **自前 grow を採用**(2パスは不要)。grow 5関数(init/grow/push/push_str/release)はすべて ≤25 行に収まり 1ファイル(`strbuf.c`)5関数ちょうど。
- **realloc は42許可関数外**のため、grow = 新サイズ malloc → 旧 data を `len` バイト while コピー → 旧 free → 差し替え、で実装。プロトタイプで実証。
- `sb_release` は末尾 `'\0'` を push(cap 不足なら grow)してから data を返し所有移譲。release しなかった構築途中 buf は `free(b.data)` で個別解放(エラー経路)。

### argv の確保方針
- **argv も自前 grow を推奨**(env_set.c の `env_append` と同型=実績あり)。事前語数カウントも可能だが、quote-aware な走査が二重に要るため grow の方が単純で Norm 内。
- 本Issueは**フィールド分割なし**なので「展開は語数を変えない」が、語数は走査するまで確定しない(クォート/空白の組合せ)ため、grow が無難。

## 2. `$` 展開の名前境界 — 【実測済み】

`var_name_len`(先頭=英字/`_`、2文字目以降=英数字/`_`)をプロトタイプで検証:

| 入力 | var_name_len | 解釈 |
|---|---|---|
| `HOME` | 4 | `$HOME` を展開 ✅ |
| `A_B1 x` | 4 | `A_B1` まで名前(空白で停止)✅ |
| `1abc` | 0 | 名前にならず → **リテラル `$`**(`$1abc`)✅ |
| `?` | 0 | `expand_var` 側で `$?` 特別分岐(名前判定の前)✅ |
| 空(行末 `$`) | 0 | リテラル `$` ✅ |

**確定した展開ルール(expand_var、q が NONE/DOUBLE のみ呼ぶ):**
1. `s[i+1]=='?'` → `$?`(last_status を10進化して push)、`i+2` を返す。
2. `is_name_first(s[i+1])` → `var_name_len` で名前長を取り、`ft_strndup`→`env_get(shell->env, name)`→非NULLなら `sb_push_str`、NULL は空(何もしない)、name を free。`i+1+len` を返す。
3. それ以外(数字/記号/空白/行末)→ `$` を1文字リテラル push、`i+1` を返す。

**メモリ順序(use-after-free 回避):** `env_get` の戻りは env 所有の借用ポインタ。`sb_push_str` で**1文字ずつコピー**してから name を free する(値ポインタは free しない)。プロトタイプの grow と同経路でリーク0を確認。

## 3. 空クォート / 連結の bash 挙動 — 【bash 実測】

| bash 入力 | 実測出力 | 設計の期待と一致? |
|---|---|---|
| `echo ''` | 空行(空文字列引数1個)| ✅ 設計「クォート開始で active=1 → 空トークン生成」と一致 |
| `echo a""b` | `ab` | ✅ 空クォートは語を割らない・連結 |
| `echo a"b c"d` | `ab cd` | ✅ DOUBLE 内空白は語内 |
| `echo $` | `$` | ✅ リテラル `$` |
| `echo $1` | **空行**(bash は位置パラメータ $1=未設定)| ⚠️ **乖離あり**(下記) |

### ⚠️ 発見した bash 乖離: `$1`(数字始まり)
- **bash:** `$1` は位置パラメータ → 未設定で空。`echo $1` は空行。
- **本設計(Architect):** minishell は位置パラメータ非対応 → `$1` は名前にならず **リテラル `$1`**。
- **判断:** minishell は位置パラメータを実装しない(スコープ外)ため、`$1`→リテラル は42 minishell では一般的で許容される割り切り。**bash と1ケース乖離するが、位置パラメータ未実装の必然**。Reviewer/PR に**既知差分**として明記。厳密 bash 一致(空展開)にしたい場合は「数字始まりは空に展開」へ変える選択肢もあるが、本Issueはリテラル方針を維持。

## 4. `$?` の数値化
- `last_status` は #4/#6 実装上 **0..255 の非負**(WEXITSTATUS / 127 / 126 / 1 / 2 / 0、builtin の 0/1)。負値が入る経路は無い(確認: execute.c の wait_status は WEXITSTATUS/128+sig=非負、builtin は 0/1)。
- よって `append_status` は **非負前提**で十分(0 は `'0'` 1個、その他は桁を逆順に溜めて反転 push)。防御的に負値分岐を入れても害なし(Architect 案どおり)。libft に ft_itoa は足さない(expand.c 内ヘルパ)。

## 5. 既存コードとの接続(事実確認)
- `src/process_line.c`: `argv = tokenize(line);` → `argv = tokenize(shell, line);` の **1行変更**。`free_argv(argv)` 経路は不変。
- `src/tokenize.c`: 現状 is_space/count_words/fill_argv/tokenize/free_argv。**`free_argv` のみ残し**、他4関数を削除して `lexer.c` 等へ置換。
- `include/minishell.h`: `char **tokenize(const char *line)` → `char **tokenize(t_shell *shell, const char *line)`。`t_buf` 定義 + 新規プロトタイプ追記。
- `src/env_utils.c` の `env_get(char **env, const char *key)` を**そのまま使用**(変更不要)。`builtins.c`/`execute.c`/`env_set.c` は展開後 argv を受けるだけで無変更。
- `Makefile`: SRCS に `lexer.c lexer_quote.c expand.c strbuf.c` を追記、`tokenize.c` 残置。

## 6. 42 Norm 注意
- 新規4ファイル。各 ≤5関数(strbuf 5 / lexer 5 / lexer_quote 3 / expand 5)。
- 状態機械の `handle_char` は分岐が多く25行超過リスク → quote 遷移を `quote_next`、`$` 起点を `handle_dollar`→`expand_var` に委譲して薄く保つ(設計どおり)。
- 三項禁止 → quote 状態分岐は if/else。while のみ。
- `t_buf` 構造体はグローバルにせずローカル/引数渡し(グローバル0個維持)。

## 7. リスク箇所(上位3)
### リスク1: 構築途中 buf / 展開 name の free 漏れ(エラー経路・未閉じクォート)
- 回避: release しなかった buf は `free(b.data)`、name は env_get 後に必ず free。未閉じクォート return 時に argv + 構築中 buf を全 free。leaks で未閉じ入力・長い展開を確認。

### リスク2: 展開と語確定の順序(`x$Y`→`xfoo` の連結)
- 回避: `$` 展開は語を確定させず同一 buf に push 継続。語確定は NONE 中の空白/行末のみ(状態機械で担保)。`x$Y`/`"$A"'$B'` を実機確認。

### リスク3: `$1` 乖離(上記2)/ 未閉じクォート status=2
- 回避: 既知差分として明記。未閉じは `last_status=2` + 行破棄(設計7)。`echo "abc`(未閉じ)で status 2 を確認。

## 8. Implementer が叩く検証コマンド
```bash
make
printf "echo '\$HOME'\n" | ./minishell       # リテラル $HOME
printf 'echo "$HOME"\necho $HOME\n' | ./minishell  # 値
printf 'echo $?\nnosuchcmd\necho $?\n' | ./minishell  # 0 -> 127
printf 'echo a"b c"d\necho a""b\necho %s\n' "''" | ./minishell  # ab cd / ab / 空
printf 'export X=hi\necho $X\n' | ./minishell  # hi
printf 'echo "abc\n' | ./minishell ; echo "status=$?"  # 未閉じ -> エラー
printf 'ls\npwd\ncd /tmp\nexport A=1\nunset A\nenv\n' | ./minishell  # 回帰
printf "echo '\$HOME'\necho \$?\nexport X=ab\necho \$X\necho a\"b c\"d\n" | leaks --atExit -- ./minishell 2>&1 | grep 'leaks for'
/tmp/pdfvenv/bin/norminette src include libft
```

## 申し送り
1. **42 ヘッダー未付与**(ログイン待ち)継続。
2. **`$1` 乖離**(リテラル扱い)を既知差分として PR/Reviewer に明記。
3. **未閉じクォート = エラー + status 2 + 行破棄**(設計7、固定)。
4. grow バッファ・$展開はプロトタイプで leaks 0 を実証済み。Implementer は本実装でも同経路を踏襲。

## 関連ファイル
- 設計入力: `docs/pipeline/issue-8/01-design.md`
- 本成果物: `docs/pipeline/issue-8/02-research.md`
