# 01-design.md

- **Issue:** #8 クォート処理(`'` `"`)+ 変数展開(`$VAR` / `$?`)
- **Stage:** 1/5 Architect

## 方針 (1行)
`tokenize` を「空白split」から **1パス文字走査の quote-aware レキサ**に差し替え、走査しながら quote 状態(none/single/double)を持ち、可変長バッファに文字を積み、ダブル/none 内のみ `$` を `env_get` / `$?` で展開して 1 トークンを組み立てる方式に統一する。可変長バッファは **realloc 不許可のため自前 grow(倍化 malloc + コピー + free)** で実装し、`tokenize` のシグネチャを `tokenize(t_shell*, const char*)` に変更して `process_line` を更新する。

## 設計方針 (5-7行)
- **アーキテクチャ:** 1パスの状態機械レキサ。`i` を進めながら quote 状態を遷移させ、`'`/`"` は除去、none 区間の空白だけで語を区切る。語の文字は `t_buf`(自前可変長文字列)に push し、`$` は none/double 内でのみ展開して値を push、シングル内は `$` もリテラル。語末で `t_buf` を `char *` 化して argv へ追加する。
- **データフロー:** `process_line(shell,line) → lex(shell,line) → char **argv → dispatch → free_argv`。展開は別パスにせず **レキサ内でインライン展開**(クォート文脈を知っているのはレキサだけなので、ここで展開しないと `'$X'` 抑制ができない)。
- **主要インターフェース:** `char **tokenize(t_shell *shell, const char *line)`(`free_argv` は不変・既存流用)。内部に `expand.c`(`$` 名前抽出・`env_get`・`$?`→数字)と `strbuf.c`(自前可変長バッファ)を新設。
- **DB変更:** なし(該当しない)。
- **エラーハンドリング:** malloc 失敗は途中まで積んだ argv を `free_argv` で全解放し `tokenize` が NULL を返す(`process_line` は既存どおり NULL を no-op 扱い)。**未閉じクォート = エラー表示してその行を実行しない**(下記7、`last_status=2`)。

## 採用理由とトレードオフ
- **採用:** 1パス・レキサ内インライン展開 + 自前 grow バッファ。理由: クォート文脈(`'$X'` は展開しない / `"$X"` はする)を持つのはレキサだけで、展開を後段 argv パスに分けると `'` か `"` の出自が失われ正しく抑制できない。
- **却下A:** 「空白split → 各 argv を後段 expand」(env API は不変で楽)。理由: split 時点でクォートを剥がすと `'`/`"` の区別が消え、`'$X'`(抑制)と `"$X"`(展開)を判別できない=仕様を満たせない。
- **却下B:** 「2パス(長さ計算→確保→書込)でバッファ確保」。理由: 展開値は env_get 結果で動的、`$?` は itoa 結果長が走査前に確定せず、長さ計算パスが本処理とほぼ同コードの二重実装になり Norm 25行/関数を圧迫。grow の方が単純。

## 1. レキサのアーキテクチャ / ファイル分割

### 1パス文字走査(推奨)とその理由
- 走査位置 `i` と quote 状態 `q`(NONE/SINGLE/DOUBLE)を持つ単一ループ。`'` は q が NONE のとき SINGLE へ(消費・出力しない)、SINGLE 中の `'` で NONE へ。`"` も同様に DOUBLE と往復。`$` は q が NONE/DOUBLE のとき展開、SINGLE のときリテラル。
- NONE 中の空白(`' '`/`'\t'`)が **語の唯一の区切り**。SINGLE/DOUBLE 中の空白は語の一部(バッファに push)。
- 「語を開いたか(active)」フラグを別に持つ。`""`/`''` のように **1文字も中身が無くてもクォートに入った時点で語を active 化**し、空文字列トークンを生成する(bash 準拠、下記2)。
- 理由(再掲・最重要): 展開可否はクォート種別に依存し、その情報は走査中のみ手元にある。後段 expand パスでは復元不能。よって **走査・クォート除去・$展開を同一パスに統合**する。

### ファイル分割(42 Norm: 1ファイル5関数 / 関数25行)
| ファイル | 関数(目安・各 ≤25 行) | 役割 |
|---|---|---|
| `src/strbuf.c` | `sb_init` / `sb_push` / `sb_push_str` / `sb_grow` / `sb_release`(5) | 自前可変長文字列(grow = malloc+コピー+free) |
| `src/lexer.c` | `tokenize` / `lex_loop` / `handle_char` / `argv_push` / `lex_cleanup`(5) | 状態機械本体・語の確定・argv 構築 |
| `src/lexer_quote.c` | `is_space` / `quote_next` / `handle_dollar`(3) | quote 遷移判定・`$` 起点処理(expand へ委譲) |
| `src/expand.c` | `expand_var` / `var_name_len` / `append_status` / `append_value` / `is_name_char`(5) | `$VAR` 名抽出・`env_get`・`$?` 数値化 |
| `src/tokenize.c`(既存) | **`free_argv` のみ残す**(他4関数は削除し lexer.c へ置換) | argv 解放(既存呼出互換) |
- `free_argv` は `process_line` から呼ばれ続けるので **tokenize.c に残置 or lexer.c へ移設、どちらか1つ**。推奨: `tokenize.c` を `free_argv` 専用に縮小し、レキサ本体は `lexer.c` に新設(diff が読みやすい)。
- 上記は **目安**。実装時に Norm 5関数/file・25行/func に収まるよう関数を寄せる/割る判断は Implementer 裁量。関数数が溢れたら `lexer_quote.c` と `expand.c` 間で再配分する。

### 関数シグネチャ(案)
| 関数 | シグネチャ案 | 備考 |
|---|---|---|
| `tokenize` | `char **tokenize(t_shell *shell, const char *line)` | **シグネチャ変更**。`process_line` の呼出を更新 |
| `expand_var` | `int expand_var(t_shell *sh, const char *s, int i, t_buf *b)` | `s[i]=='$'` 前提。push 後の **次index** を返す |
| `sb_push` | `int sb_push(t_buf *b, char c)` | 失敗=非0(以降 argv を畳んで NULL) |
| `sb_push_str` | `int sb_push_str(t_buf *b, const char *s)` | env 値・`$?` 文字列の連結に使用 |

## 2. トークン化の状態機械(quote / 区切り / 連結 / エッジ)

### 状態と遷移
| 現状態 | 入力 | 遷移 / 動作 |
|---|---|---|
| NONE | 空白 | 語が active なら **語を確定**(argv へ push、buf reset、active=0)。非activeなら読み飛ばし |
| NONE | `'` | SINGLE へ。active=1(文字は出さない) |
| NONE | `"` | DOUBLE へ。active=1 |
| NONE | `$` | active=1 → `expand_var`(展開して push) |
| NONE | その他 | active=1 → buf に push |
| SINGLE | `'` | NONE へ(出さない) |
| SINGLE | その他(`$`・空白含む) | buf に **リテラル** push(展開しない) |
| DOUBLE | `"` | NONE へ(出さない) |
| DOUBLE | `$` | `expand_var`(展開して push) |
| DOUBLE | その他(空白含む) | buf に push |
| 行末 | q != NONE | **未閉じクォート → エラー(下記7)** |
| 行末 | q == NONE & active | 最後の語を確定 |

- **語の開始:** active=0 から非空白(クォート開始 / 通常文字 / `$`)を見た瞬間。
- **語の終了:** NONE 状態で空白を見た時、または行末。
- **隣接断片の連結:** 語の確定は **空白を見たときのみ**。クォートの開閉・展開・通常文字は同じ `buf` に積み続けるため、`a"b c"d` は `a` + `b c`(空白はDOUBLE内なので語内)+ `d` が 1 buf に連結し **`ab cd`** という 1 トークンになる。
- **連結例:** `'a'b"c"` → `abc`(1語)。`x$Y` で `Y=foo` → `xfoo`。`"$A"'$B'` → A の値 + リテラル `$B`。

### エッジケース一覧
| 入力 | 期待 | 根拠 |
|---|---|---|
| `''` / `""` | **空文字列トークン1個**(argv 要素 `""`) | クォートに入った時点で active=1。bash 準拠(下記末尾で裏取り) |
| `a""b` | `ab`(1語) | 空クォートは語を割らない、連結 |
| `echo ''` | argv = `{echo, ""}`(2要素、第2は空文字列) | 空トークン生成。`echo` は空行を出力 |
| `a"b c"d` | `ab cd`(1語) | DOUBLE 内空白は語内 |
| `'$HOME'` | リテラル `$HOME` | SINGLE 内は展開しない |
| `"$HOME"` | HOME の値 | DOUBLE 内は展開する |
| 連続空白 `a   b` | `{a, b}` | NONE 空白は1区切り扱い(連続でも語は割れない) |
| 末尾空白 `ls ` | `{ls}` | 行末で active な語のみ確定、末尾空白は無視 |
| 空行 / 空白のみ | argv = `{NULL}`(=`!argv[0]`) | `process_line` が no-op。active な語が無い |

## 3. 展開の設計(`$VAR` / `$?` / 境界規則)

### `$` 起点処理(`expand_var`、q が NONE / DOUBLE のときだけ呼ぶ)
1. `s[i]=='$'`。次文字 `c = s[i+1]` を見る。
2. **`c=='?'`:** `$?` → `append_status`(`shell->last_status` を10進文字列化して push)。`i+2` を返す。
3. **`c` が名前先頭文字(英字 or `_`):** `var_name_len` で名前を伸ばす(英数字 or `_`、**先頭は非数字**=2文字目以降は数字可)。`ft_strndup` で名前を切り出し → `env_get(shell->env, name)` → 非NULLなら `sb_push_str`、NULL(未定義)なら **何も push しない=空**。name を free。`i + 1 + namelen` を返す。
4. **`c` がそれ以外(空白 / `"` / `$` / 数字 / 行末など):** `$` を **リテラル**として1文字 push。`i+1` を返す。
   - 例: `$`(行末)→ `$`。`$1`(数字始まり)→ 名前にならず `$1` リテラル(`$` push 後 `1` は通常文字として後続ループで push)。`$"x"` → `$` リテラル + その後 `"` で DOUBLE 開始。`$$`(PID)はスコープ外 → `$` リテラル + 次の `$` を再評価(=`$$` がそのまま出る)。

### 名前境界(`is_name_char`)
- 名前先頭: `ft_isalpha(c) || c=='_'`(libft に isalpha が無ければ自前判定 `(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'`)。
- 名前2文字目以降: 上記 + `(c>='0'&&c<='9')`。
- `var_name_len(s, start)`: 先頭規則を満たさなければ長さ0を返し、呼び側はリテラル `$` 扱いに分岐(上記4と整合)。

### クォート文脈での展開制御
- SINGLE 中は `expand_var` を**呼ばない**(`$` を素の文字として push)。これは状態機械(2節)の SINGLE 行で担保。
- DOUBLE / NONE 中のみ `expand_var` を呼ぶ。**ダブルクォートと素状態で展開の有無は同じ**(差はクォート内空白が語を割るか否かだけ)。

## 4. 可変長バッファ(realloc 不許可 → 自前 grow)

### データ構造 `t_buf`(strbuf.c)
```
typedef struct s_buf { char *data; size_t len; size_t cap; } t_buf;
```
- `sb_init(b)`: `cap=16` 程度を `malloc`、`len=0`、`data[0]` は未NUL(release 時に確定)。失敗で非0。
- `sb_push(b, c)`: `len==cap` なら `sb_grow` → 失敗なら非0返し。`data[len++]=c`。
- `sb_grow(b)`: **新 cap = cap*2** で `malloc` → 旧 `data` を `len` バイト手動コピー(while ループ)→ 旧 `free` → 差し替え。realloc 相当を自前実装。失敗で非0(旧 data は free しないで温存し呼び側が畳む)。
- `sb_push_str(b, s)`: `s` を1文字ずつ `sb_push`(env 値・`$?` 文字列に使用)。
- `sb_release(b)`: 末尾に `'\0'` を入れ(cap 不足なら grow)、`data`(= 完成した C 文字列)を返し、`b` の所有を手放す(以降 reset)。これが argv 要素になる。

### 採用方針(明確化)
- **自前 grow(倍化 malloc + コピー + free)を採用。2パスは採用しない。**
- 理由(再掲): 展開後の最終長が走査前に確定しない($? itoa 長・env 値長が動的)ため 2パスは「ほぼ同じ走査の二重実装」になり Norm 25行/関数を超えやすい。grow は push/grow/release の3関数に収まり 25行内。
- **後段 Investigator へ:** 自前 grow と 2パスのどちらが Norm 25行に収まりリークしないかを実コードで裏取り(下記末尾)。grow 採用が前提だが、grow が 25行超過/リーク困難なら 2パスへ切替判断。

## 5. `$?` の数値化(ft_itoa 相当)

- **方針: `expand.c` 内ヘルパ `append_status(t_buf *b, int n)` として実装**(libft に ft_itoa を**足さない**)。理由: 用途が `$?` だけで、`last_status` は **0..255 の非負**(終了ステータス由来)なので、汎用 ft_itoa(符号・INT_MIN 対応)は過剰。expand 内に閉じる方が変更面が小さく再リンク影響も局所。
- **実装(負値考慮):** `last_status` は常に非負想定だが、防御的に `n<0` のとき `'-'` を push してから絶対値処理(`while` で桁を逆順バッファに溜め、反転 push)。0 のときは `'0'` を1個 push。INT_MIN は exit status 由来では発生しないため特別扱い不要(必要なら `long` で受ける、ただしスコープ外)。
- **却下案:** libft に `ft_itoa` を新設。理由: libft 改変は他Issueへ波及し再リンク範囲が広がる。`$?` 限定なら expand 内ヘルパで足り、Norm も守りやすい。

## 6. メモリ管理(所有権 / free 経路 / 失敗時不変条件)

| 対象 | 所有者 | free 経路 |
|---|---|---|
| `t_buf.data`(語構築中) | レキサのローカル `t_buf` | `sb_release` で argv へ移譲。途中エラー時は `sb_release` せず `free(b.data)` |
| argv 要素(`sb_release` の戻り) | argv 配列 | `process_line` 末尾の `free_argv`(既存・不変) |
| 展開一時 `name`(`$VAR` 名) | `expand_var` ローカル | `ft_strndup` した name は `env_get` 後 **必ず free**(値は env 所有なので別途コピー不要、`sb_push_str` で逐次コピー) |
| env 値(`env_get` の戻り) | `shell->env`(借用) | **free しない**(env 所有。push_str でコピーするだけ) |
| argv 外枠 | `tokenize` が確保 | `free_argv` |

### 失敗時に argv を壊さない不変条件
- `tokenize` は **argv を成長させる方式**(`argv_push`: 現要素数+2で新外枠 malloc → 旧要素 shallow 移譲 → 新要素追加 → 旧外枠 free → 末尾 NULL)、または **事前に語数上限を見積もる**のどちらか。可変長 argv も grow と同型。
  - 推奨: argv も自前 grow(`argv_push`)。語数は走査前に確定しない(展開で増減しない=語数は区切りで決まるので **クォート/展開を無視した空白語数の上限**で 1パス事前カウントも可)。**Investigator に「事前カウント可否」を裏取りさせる**(展開は語数を変えないので、クォート除去後の空白区切り数=最終語数の上限になり、事前 malloc 1回で済む可能性が高い)。
- どの malloc 失敗でも、**それまでに argv に積んだ完成トークン + 構築中 buf を全 free** してから `tokenize` が NULL を返す。`process_line` は `!argv` を `free_argv(NULL)`(NULLガード済)で no-op 化するため整合。
- **リーク重点:** ① 構築中 `t_buf.data` の解放漏れ(release しなかった語)、② 展開 name の free 漏れ(early return 経路含む)、③ argv grow 時の旧外枠 free 忘れ/要素二重 free、④ 未閉じクォートでエラー return する際の buf/argv 解放。

## 7. 未閉じクォートの方針(1つに固定)

- **採用: エラー表示 + その行を実行しない。** 行末で `q != NONE` の場合、`tokenize` は構築中の argv/buf を全 free し **NULL を返す**。`process_line` 側で「未閉じ」を区別するため、レキサが `stderr` に `minishell: unexpected EOF / unclosed quote` 相当を出し、`shell->last_status = 2`(bash の構文エラー status)を立てる。
- **理由:** マルチライン継続(`"` の途中で改行して継続入力)は **スコープ外**(Issue 明記)。よって「閉じたものとして扱う」より「行を捨ててエラー」が安全・予測可能で、後続のヒアドキュメント/継続Issueと衝突しない。
- **実装上の注意:** `tokenize` の NULL 返却は「malloc 失敗」と「未閉じ」の2系統が混在する。**未閉じ時のみ `last_status=2` を立てる**(malloc 失敗は status を触らないか別値)。`process_line` は NULL を一律 no-op で良いが、status 設定はレキサ内で完結させる(`shell` を受け取っているので可能)。

## 8. スコープ / やらないこと / 後続拡張

### スコープ(影響範囲・変更オーダー)
- **新規:** `src/lexer.c` / `src/lexer_quote.c` / `src/expand.c` / `src/strbuf.c`。`include/minishell.h` に `t_buf` 定義(または `strbuf.h` 新設)+ プロトタイプ追記。
- **変更:** `src/tokenize.c`(旧4関数削除、`free_argv` のみ残す)、`src/process_line.c`(`tokenize(line)` → `tokenize(shell, line)` の1行)、`include/minishell.h`(`tokenize` プロトタイプ変更 + 新規プロトタイプ)、`Makefile`(SRCS に新規4ファイル追記、`tokenize.c` は残置)。
- **無変更:** `env_utils.c`(`env_get` をそのまま利用)、`builtins.c` / `execute.c` / `env_set.c`(展開後の argv を受けるだけ、API 不変)。
- **想定行数:** minishell 側 200-300 行。1PR で完結可能。Norm/行数で溢れる場合は「(A) クォート除去のみ(展開なし)」と「(B) `$` 展開追加」に2分割可だが、A単体では `'$X'`/`"$X"` の差を確認できず DoD が中途半端になるため **本Issueは1PR(クォート+展開セット)で進める**。

### やらないこと(3点)
1. **展開結果のフィールド分割**(`X="a b"; echo $X` → bash は 2 引数。本Issueは **1引数のまま**)・**ワイルドカード `*`**・**`\` エスケープ**・**`$$`/`${}`/`$(...)`/`` `...` ``** — 別Issue。`$` 展開は素の `$VAR` と `$?` のみ。
2. **未閉じクォートのマルチライン継続入力**(`"` 途中で改行 → 継続プロンプト)— 別Issue。本Issueは未閉じ=エラーで行破棄(7節)。
3. **`;` / `&&` / `||` / リダイレクト(`<` `>` `>>` `<<`)/ パイプ `|`** — 別Issue。レキサは語区切りまで。演算子トークン化は後続でこの状態機械に「NONE 中のメタ文字検出」を足して拡張する。

### 後続拡張ポイント
- **フィールド分割:** `$VAR` 展開を NONE 中で行う際、push 前に **展開値中の空白で語を割る**フックを `expand_var` の戻り処理に差す(DOUBLE 中は割らない)。本設計の「展開はレキサ内」だからこそ後付けが自然。
- **リダイレクト/パイプ:** `handle_char` の NONE 分岐に `< > |` 検出を足し、語ではなく演算子トークンを emit。argv → コマンドリスト(`t_cmd`)へデータ構造を拡張。
- **`${}` / `$(...)`:** `expand_var` の `$` 直後分岐に `{` / `(` ケースを追加。名前抽出ロジック(`var_name_len`)を流用。

## 9. 完了条件(DoD)— Implementer / Reviewer 用チェックリスト
- [ ] `echo '$HOME'` → リテラル `$HOME`(展開されない)。
- [ ] `echo "$HOME"` → HOME の値。`echo $HOME` も同値。未定義 `echo $NOPE` → 空行。
- [ ] `echo $?` → 直前コマンドの終了ステータス(数字)。`false; echo $?`(false が無ければ存在しないコマンド)で非0が出る。
- [ ] `echo a"b c"d` → `ab cd`(1引数)。`echo 'a'b"c"` → `abc`。
- [ ] `export X=hi; echo $X` → `hi`(#6 の export 経由で env に入り、本Issueの展開で見える)。
- [ ] クォート除去: `echo "hello"` → `hello`(引用符なし)。`echo ''` → 空行(空トークン1個)。
- [ ] `$` リテラル: `echo $` → `$`。`echo "$"` → `$`。`echo $1`(数字始まり)→ `$1`。
- [ ] 空クォートの連結: `echo a""b` → `ab`。
- [ ] **既存回帰:** `ls` / `pwd` / `cd /tmp` / `export A=1` / `unset A` / `env` / `exit` が従来どおり(クォート/`$`を含まない入力で挙動不変)。
- [ ] **未閉じクォート** `echo "abc`(閉じない)→ エラー表示、行を実行しない、`$?`(=last_status)が 2。
- [ ] **メモリリークなし**(leaks/valgrind): 構築中 buf・展開 name・argv grow・未閉じエラー経路・空トークン・長い展開(grow が複数回走る入力)。
- [ ] **42 Norm:** 関数25行以内・1ファイル5関数以内・while のみ(for/三項/switch なし)・グローバル0個・80列。
- [ ] `make` が `-Wall -Wextra -Werror` で警告なし。**二度目の `make` で不要な再リンクが走らない**(新規4ファイルは SRCS に明示追記、`tokenize.c` 残置)。
- [ ] `process_line` / `dispatch` / `run_external` / `free_argv` の外部シグネチャ・argv 所有権・free 経路が維持(変更は `tokenize` シグネチャと内部のみ)。

---

## 後続 Investigator への裏取り依頼(実コードで確認)
1. **可変長バッファの実装選択(grow vs 2パス):** 自前 grow(倍化 malloc + 手動コピー + free)と 2パス(長さ計算→確保→書込)のどちらが **Norm 25行/関数・5関数/file に収まり、かつリークしない**かを、実際にスケルトンを書いて行数とリーク(leaks 実行)で裏取りすること。本設計は grow 採用前提だが、grow が 25行に収まらない/release のNUL確保でリークが出るなら 2パスへ切替判断。`argv` 自体を grow するか **事前に空白区切り語数をカウントして 1回 malloc** で足りるか(展開は語数を変えないという仮説の検証含む)も実コードで確認。
2. **`$` 展開の名前境界:** (a) `$1`(数字始まり)が `$1` リテラルになるか、(b) `$_`/`$A1`/`$A_B` が正しく名前として伸びるか、(c) `$`(行末)・`$"`・`$ `(空白)・`$$` がリテラル `$` 扱いになるか、(d) `env_get` の戻り(value ポインタ借用)を push_str でコピーして name を free する順序でリーク/use-after-free が無いか、を実コマンド + leaks で裏取りすること。
3. **空クォート `""` / `''` の argv 要素生成:** bash で `echo ''` が空文字列引数 1 個を渡す(`echo` が空行を出す)挙動を実機で確認し、本設計の「クォート開始で active=1 → 空トークン生成」が bash と一致するか、また `a""b`→`ab`(空クォートが語を割らない)を実コードで裏取りすること。`$?` の数値化は **非負(0..255)前提**で良いか、`last_status` に負値が入る経路が #4/#6 実装に無いかも併せて確認。
