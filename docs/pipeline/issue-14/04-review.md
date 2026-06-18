# 04-review.md

- **Issue:** #14 シグナル(ctrl-C / ctrl-D / ctrl-\)+ 唯一のグローバル変数
- **Stage:** 4/5 Reviewer
- **対象:** ブランチ `feat/issue-14-signals` / コミット `fa3c699`
- **レビュー方式:** Implementer 報告を信用せず、メインが独立にビルド・Norm 監査・グローバル規約検査・**ハンドラ async-safety 検査**・**pty 再検証**を実行。

## 総評
**承認(差し戻し不要)。** must 級なし。全 DoD(対話 ctrl-C / 実行中断 130・131 / ctrl-\ 無視 / ctrl-D / 唯一のグローバル)を独立検証で確認。**これでマンダトリーが揃う。** Norm は 42 ヘッダー(保留)を除きクリーン。should/既知の制限(heredoc ctrl-C スコープ外)を文書化。

## 独立検証
| 観点 | 結果 |
|---|---|
| `make re` ビルド / 再リンクなし | ✅ |
| 関数行数(≤25)/ 5関数/file | ✅(signals4/repl2/process_line4/execute5/pipeline_child3)|
| 禁止構文 | ✅ なし |
| **グローバル変数** | ✅ **`sig_atomic_t g_signal` ただ1個**(column-0 grep)。番号のみ |
| **ハンドラ async-signal-safe** | ✅ `printf`/`malloc`/`free` 不使用(write + rl_* + g_signal 代入のみ)|
| **ハンドラの規約遵守** | ✅ `shell`/`last_status`/t_cmd へのアクセスなし(g_signal のみ)|
| norminette | ⚠️ `INVALID_HEADER` のみ(33ファイル)|
| メモリリーク(非フォーク)| ✅ 0 leaks |

### 対話シグナル(独立 pty 再検証: Python pty)
- 空プロンプト ctrl-C → `$?`=**130** ✅
- `sleep` 中 ctrl-C → 中断・`$?`=**130** ✅
- `sleep` 中 ctrl-\ → Quit・`$?`=**131** ✅
- 対話プロンプト ctrl-\ → 無視・シェル生存 ✅
- 入力途中 ctrl-C → 行破棄 ✅
- ctrl-D → 終了 ✅
- パイプ対話 `echo X | cat` → 動作 ✅(回帰)

## 観点別レビュー

### 設計準拠
- ✅ 唯一のグローバル `g_signal`(番号のみ)。**`last_status=130` は repl_loop が確定**(ハンドラは t_shell 不可視)= 42 規約「グローバルは受信番号のみ・データアクセス禁止」を構造的に遵守。
- ✅ 対話=SIGINT 新プロンプト(`rl_on_new_line`/`rl_replace_line`/`rl_redisplay`)+ SIGQUIT 無視 / 実行親=SIG_IGN / 子=SIG_DFL。`wait_status`(128+sig)で 130/131。
- ✅ repl_loop が毎反復先頭で `set_signals_interactive()` → 実行モードのまま残らない(復帰漏れなし)。
- 妥当な逸脱: 親 SIG_IGN を `process_line` の exec 前 1箇所に集約(run_external の25行を保ちつつ単一+パイプを covers)。

### バグ / 安全性
- 致命バグなし。`sigint_prompt` は SA_RESTART 下で readline を中断せず新プロンプトを描画(pty で実証)。
- 子の `reset_signals_child()` は fork 後・execve 前(`child_run_external` / `pipe_child` 先頭)→ 実行コマンドが ctrl-C/ctrl-\ で正しく死ぬ(130/131)。`setpgid` なしでも同一フォアグラウンド PG で成立(pty で実証)。
- ハンドラ async-signal-safe(write + sig_atomic_t + readline 提供 API)。`g_signal` のクリアは repl_loop の対話側のみで一貫。

### セキュリティ / Norm
- ✅ 許可関数(sigaction/signal/sigemptyset/kill 系/rl_*)のみ。グローバル1個・25行/5関数・while のみ。

## 指摘ログ

### must — なし

### should / 既知の制限(別Issue・非ブロッキング)
1. **heredoc 中 ctrl-C はスコープ外** — `run_heredocs` は親 SIG_IGN 後に走るため heredoc 入力中の ctrl-C は無視される(bash は中断)。SA_RESTART 一時解除 or 別ハンドラが要り複雑 → 後続Issue。コマンドプロンプト + 実行中断は確実に満たす(Issue でも最小/降格を許容済み)。
2. **親 builtin 実行中の ctrl-C は無視**(`process_line` で exec 前 SIG_IGN のため)— builtin は短時間で実害小。bash も親 builtin 中の ctrl-C は限定的。
3. **中断時の追加改行なし** — 子の SIG_DFL 終了で端末が `^C` 表示。bash との微差は端末依存(should、pty で 130/131 と動作は一致)。

### nice
- `bi_exit` がパイプ内で `exit` を出力(#2 由来の既知 should、本Issue範囲外)。

## 次段(Integrator)への申し送り
- 差し戻し往復: **0 回**(must なし)。段階5 へ進んでよい。
- 本ブランチは main 分岐 → PR ベースは **main**。
- PR 本文に「シグナルが動く=**マンダトリー完成**」「唯一のグローバル g_signal(番号のみ)」「DoD 充足」「既知(heredoc ctrl-C スコープ外)」を明記。`Closes #14`。
- **これで 42 Minishell マンダトリーの主要機能が一通り揃う**(残りは 42 ヘッダー付与と README、Bonus)。
