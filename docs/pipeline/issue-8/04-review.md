# 04-review.md

- **Issue:** #8 クォート処理 + 変数展開($VAR / $?)
- **Stage:** 4/5 Reviewer
- **対象:** ブランチ `feat/issue-8-quotes`(初回 `e617922` → 差し戻し修正後 再検証)
- **レビュー方式:** Implementer 報告を信用せず、メインが独立にビルド・Norm 監査・**敵対的エッジ入力**・leaks を再実行。

## 総評
**承認(差し戻し1往復で解消)。** 独立レビューで **must 級バグ1件**(不要 unquoted 空展開が空引数を生成)を検出 → 段階3へ差し戻し → Implementer 修正 → 再検証で解消。その他 must なし。should/nice/既知差分は別Issue or 文書化。

## 差し戻しログ(round-trip 1 / 最大2)
### 🔴 must-1: unquoted の空展開が空引数を生成(bash 乖離)→ **修正済み**
- **症状:** `$NOPE`(未定義・クォートなし)単独入力で argv=`{""}` を生成し、空コマンド `""` を exec しようとして `minishell: : Permission denied` を出した。bash は **何もしない**(unquoted で空に展開した語は除去)。`echo $NOPE` の引数個数も bash(1)と乖離(2)していた。
- **根因:** レキサが `$` を見た時点で `active=1` にし、展開結果が空でも語を確定していた。bash は「クォートを含む語」or「非空の語」だけを残す。
- **修正:** `t_lex` に `had_quote` を追加。クォート開始時に `had_quote=1`。`finish_word` で **`!had_quote && 語が空` のときは語を捨てる**(クォート付き空 `""`/`"$NOPE"` は残す)。
- **再検証(独立実測):**
  - `$NOPE`(単独)→ **no-op**(エラーなし)✅
  - `echo $NOPE` → argv=`{echo}`(空行出力)✅ bash 一致
  - `echo "$NOPE" END` → ` END`(空引数を保持)✅
  - `echo ''` → 空引数保持 ✅
  - 全回帰(`'$HOME'`/`"$HOME"`/`$?`/`export;$X`/`a"b c"d`/`a""b`)✅、unclosed→status 2 ✅、leaks 0 ✅、Norm クリーン(`INVALID_HEADER` のみ)、関数 ≤25 行。

## 独立検証(修正後)
| 観点 | 結果 |
|---|---|
| `make re` ビルド / 再リンクなし | ✅ |
| 関数行数(≤25)/ 5関数/file | ✅(strbuf 5 / lexer 5 / lexer_state 5 / expand 4)|
| 禁止構文 / グローバル | ✅ なし / 0個 |
| norminette | ⚠️ `INVALID_HEADER` のみ |
| leaks(quotes/expand/grow/error 経路)| ✅ 0 |

### 敵対的エッジ(独立実行)
- `echo "'"`→`'`、`echo '"'`→`"`(混在クォート)✅
- `echo "abc$"`→`abc$`($末尾リテラル)✅
- `$A$B`(x,y)→`xy`(隣接変数連結)✅
- `echo X$NOPE Y`→`X Y`(語中の空展開は語を消さない)✅
- `echo "   " end`→空白保持 ✅
- `echo "$HOME$HOME"`→値連結 ✅

## 指摘ログ

### must — 1件(上記 must-1、修正済み・解消)

### should / 既知差分(別Issue・非ブロッキング)
1. **`$1`(数字始まり)= リテラル `$1`** — bash は位置パラメータ(空)。位置パラメータ非対応の必然。
2. **未閉じクォート = エラー + status 2 + 行破棄** — マルチライン継続は別Issue。

### nice / 既知の制限
1. **タブ文字がクォート内で保持されない** — 独立調査の結果、**readline が非対話パイプ入力で TAB を消費**(完成補完キー)しており、レキサに届く前に失われている(空白は同一コード経路で保持されることを確認済み=レキサは正しい)。`rl_bind_key('\t', rl_insert)` で literal tab にできるが対話補完に影響するため別途判断。42 評価でタブ入力は稀。
2. **フィールド分割なし**(`X="a b"; echo $X` は1引数)— 設計どおりスコープ外。

## 次段(Integrator)への申し送り
- 差し戻し往復: **1 回**(must-1 を修正して解消、上限2以内)。段階5 へ進んでよい。
- 本ブランチは main 分岐 → PR ベースは **main**。
- PR 本文に「クォート + 展開が動く」「DoD 充足」「既知差分($1/未閉じ/tab-readline)」「フィールド分割は別Issue」を明記。`Closes #8`。
