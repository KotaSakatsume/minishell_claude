# 04-review.md

- **Issue:** #10 リダイレクト < > >> << (heredoc) + コマンド構造体
- **Stage:** 4/5 Reviewer
- **対象:** ブランチ `feat/issue-10-redirect` / コミット `003b532`
- **レビュー方式:** Implementer 報告を信用せず、メインが独立にビルド・Norm 監査・**敵対的エッジ入力**・fd/メモリ検証を再実行。

## 総評
**承認(差し戻し不要)。** must 級なし。argv→t_cmd の構造移行 + `< > >> <<` + heredoc の全 DoD を独立検証で確認。Norm は 42 ヘッダー(保留)を除きクリーン。should 1件(`> $NOPE` の ambiguous redirect 乖離)+ 既知の制限を文書化。

## 独立検証
| 観点 | 結果 |
|---|---|
| `make re` ビルド / 再リンクなし | ✅ |
| 関数行数(≤25)/ 5関数/file | ✅ 全新規ファイルで超過ゼロ(lexer5/lexer_op4/parser5/cmd5/redir5/heredoc3/process_line4/execute4)|
| 禁止構文 / グローバル | ✅ なし / 0個 |
| norminette | ⚠️ `INVALID_HEADER` のみ(29ファイル)|
| メモリリーク(非フォーク経路)| ✅ 0 leaks |
| fd リーク | ✅ `/dev/fd` が bash と同一 |

### 機能・エッジ(独立実行)
- `echo hi > f; cat < f`→`hi`、`>>` append、`pwd > f; pwd`(builtin 退避/復元で端末復帰)✅
- `cat << EOF` heredoc、`cat << A << B`(両方読み**最後が stdin**=`b-content`)✅
- `echo x>y`(空白なし隣接)→ `>` トークン分離 ✅、`echo ">"`→リテラル ✅
- open 失敗 ENOENT/EACCES/**EISDIR** → メッセージ + status 1 非実行 ✅
- `ls >` / `>` / `<<` / `>>` 単独 → syntax error status 2、**クラッシュなし**(`survived` 出力)✅
- 複数 redir `> a > b`(b 有効)/ `< a < b`(b 有効、a の fd は dup2 前 close)✅
- `> f`(コマンド無し)→ 空作成 status 0 ✅
- 回帰: `ls`/`pwd`/`cd`/`export`/`unset`/`echo`/`'`/`"`/`$VAR`/`$?` 不変 ✅

## 観点別レビュー

### 設計準拠
- ✅ 型付きトークン列 → パーサ → `t_cmd`。レキサは NONE のみ演算子検出、`handle_single/double` 無変更でクォート内リテラル(#8 を破壊せず)。
- ✅ 外部=子で `apply_redirs`(dup2)→ execve、ビルトイン=親で `dup` 退避 → 適用 → `restore_fds`。redir-only は退避/復元のみで file 作成。
- ✅ `op_type` 戻り型 enum→int は norminette 回避の妥当な微調整(enum 値は int 互換)。

### バグ / fd・メモリ
- 致命バグなし。**fd 管理が正しい:** `apply_one` は dup2 後 open fd を close、heredoc は適用時 `r->fd=-1` で `free_redirs` の二重 close を防止。外部は子で dup2(親の fd は `free_cmd`→`free_redirs` で回収)。`< a < b` で a の fd が b の dup2 前に close される(出力 B + fd リークなしで実証)。
- str 移譲: WORD→argv / REDIR 直後 WORD→target、移譲後 `tok->str=NULL`。`free_tok` が残存 str を回収。構文エラー early return で `free_cmd`+`free_tok` を通る(leaks 0 で実証)。
- child_exec は `errno` を execve 直後に退避してから write(errno 汚染回避)。path を free して exit。

### セキュリティ
- ✅ エラー出力は write 固定文字列 + target のみ。open フラグ/mode 0644 妥当。許可関数(open/close/dup/dup2/pipe/read)のみ。

### 可読性
- ✅ レキサ(トークン化)/ パーサ / redir 適用 / heredoc / cmd 管理が責務分割され読みやすい。fd の所有・close 箇所がコメントで明示。

## 指摘ログ

### must — なし

### should(別Issue / 非ブロッキング)
1. **リダイレクト先が空展開(`echo hi > $NOPE`)→ "syntax error" status 2** — bash は **"ambiguous redirect" status 1**。本実装は #8 の「unquoted 空展開語は除去」により、target が消えて「対象欠落=構文エラー」に見える。bash 完全一致には parser に「展開前トークンが存在したが空/複数語」の状態を渡す必要があり、フィールド分割(別Issue)と地続き。**エラーで非実行・非ゼロ status になる点は妥当**で、コード/メッセージのみ乖離。

### nice / 既知の制限
1. **heredoc:** 変数展開しない(最小固定)/ 大容量(pipe バッファ ~64KB 超)はデッドロックしうる=スコープ外 / `<<` 中の Ctrl-C 中断はシグナルIssueで。
2. **外部フォーク経路のメモリリーク**は `leaks --atExit` が fork 子の atexit で解析ハングのため自動計測不可 → code review で担保(child の path free、execve 置換、親 free_cmd)。Reviewer も child_exec / run_external を目視確認し漏れなしと判断。

## 次段(Integrator)への申し送り
- 差し戻し往復: **0 回**(must なし)。段階5 へ進んでよい。
- 本ブランチは main 分岐 → PR ベースは **main**。
- PR 本文に「リダイレクト+heredocが動く」「argv→t_cmd移行」「DoD充足」「既知($NOPE ambiguous / heredoc展開なし / 大容量 / fork-leak は review担保)」を明記。`Closes #10`。次の自然な単位は **パイプ `|`**(t_cmd を `next` でリスト化、本設計の apply_redirs/run_heredocs を再利用)。
