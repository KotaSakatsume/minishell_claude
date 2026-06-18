# 03-implementation.md

- **Issue:** #14 シグナル(ctrl-C / ctrl-D / ctrl-\)+ 唯一のグローバル変数
- **Stage:** 3/5 Implementer
- **Branch:** `feat/issue-14-signals`(main = #2+#4+#6+#8+#10+#12 の上)
- **実行者:** パイプライン本体(メインが直接実行。対話は `expect`/Python pty で検証)

## 作成 / 変更ファイル
| ファイル | 種別 | 役割 |
|---|---|---|
| `include/minishell.h` | 変更 | `#include <signal.h>`、`extern sig_atomic_t g_signal;`、`signals.c` プロト3本 |
| `src/signals.c` | 新規 | `g_signal` 定義 / `sigint_prompt`(ハンドラ)/ `set_signals_interactive` / `set_signals_exec_parent` / `reset_signals_child` |
| `src/repl.c` | 変更 | 反復先頭で `set_signals_interactive()`、readline 後に `g_signal==SIGINT → last_status=130` + クリア |
| `src/process_line.c` | 変更 | exec 分岐の前に `set_signals_exec_parent()`(単一/パイプ共通で親を SIG_IGN)|
| `src/execute.c` | 変更 | `child_run_external` 先頭で `reset_signals_child()` |
| `src/pipeline_child.c` | 変更 | `pipe_child` 先頭で `reset_signals_child()` |
| `Makefile` | 変更 | SRCS に `src/signals.c` |

## 設計からの逸脱
- **親の SIG_IGN 設置を `process_line` の exec 分岐直前に集約**(設計は run_external/exec_pipeline 各々だったが、1箇所で単一+パイプ両方を covers し、`run_external` の25行を超えないため)。子の `reset_signals_child` は `child_run_external` / `pipe_child` 先頭(設計どおり)。
- **heredoc 中 ctrl-C は本Issueでスコープ外に降格**(調査どおり)。理由: `run_heredocs` は `set_signals_exec_parent()`(親 SIG_IGN)後に走るため heredoc 入力中の ctrl-C は無視される。bash 同等の heredoc 中断は SA_RESTART 一時解除 or 別ハンドラが要り複雑 → 後続Issue。コマンドプロンプト + 実行中断は確実に満たす。

## 検証(すべて実測 / Intel Mac, Apple clang 14。対話は pty 経由)

### ビルド / Norm / グローバル
- `make re` 警告/エラーなし、二度目 `make` 再リンクなし。
- `norminette src include libft` → **`INVALID_HEADER` のみ(33ファイル)**。
- 全関数 ≤25行、新規/変更各ファイル ≤5関数(signals 4 / repl 2 / process_line 4 / execute 5 / pipeline_child 3)。
- **ファイルスコープのグローバルは `sig_atomic_t g_signal` ただ1個**(column-0 grep で確認)。番号のみ保持(t_shell/last_status を持たない)。

### 対話シグナル(Python pty ドライバ + expect で実測)
| ケース | 結果 |
|---|---|
| 空プロンプトで ctrl-C → 新プロンプト + `echo $?` | **130** ✅ |
| 入力途中で ctrl-C → 行破棄して新プロンプト(`echo cleared` が走る)| **OK**(`rl_replace_line`)✅ |
| `sleep 30` 実行中に ctrl-C → 即中断・シェル生存・`$?` | **130** ✅ |
| `sleep 30` 実行中に ctrl-\(SIGQUIT)→ 子 Quit・シェル生存・`$?` | **131** ✅ |
| 対話プロンプトで ctrl-\ → 無視・シェル生存(`echo alive`)| **ALIVE** ✅ |
| ctrl-D(EOF)→ 終了 | ✅(全 pty テストが `\x04` で正常終了)|

### 回帰
- パイプ対話(`echo hi | cat | tr a-z A-Z` → `HI`)✅
- 非対話: 通常実行 / パイプ / リダイレクト / cd・export・unset / クォート・展開 不変 ✅
- メモリリーク: 非フォーク経路(echo/cd/export/unset)`leaks --atExit` → **0 leaks**(シグナル機能は動的確保なし)✅

## Reviewer への申し送り
1. **【既知・受容】42 ヘッダー未付与** — `INVALID_HEADER` のみ。must 対象外。
2. **唯一のグローバル `g_signal`(番号のみ):** ハンドラは `g_signal=sig` + write + readline 再表示のみ。**`last_status=130` は repl_loop が確定**(ハンドラは t_shell 不可視=規約厳守)。実行中断は `wait_status`(128+sig)で 130/131。
3. **親 SIG_IGN / 子 SIG_DFL:** 親は `process_line` で exec 前に SIG_IGN、子は child 関数先頭で SIG_DFL。`setpgid` 不要(子が同一フォアグラウンドPG)。
4. **対話復帰:** repl_loop が毎反復先頭で `set_signals_interactive()` → 実行モードのまま残らない。
5. **SA_RESTART** 付き(対話 SIGINT)で readline 中断せずハンドラが新プロンプト描画。
6. **heredoc 中 ctrl-C はスコープ外**(上記逸脱)。明記済み。
7. **対話テストは pty 必須**(非対話パイプではプロンプトが出ない)。Python pty / expect で検証済み。

## ブランチ / コミット
- ブランチ: `feat/issue-14-signals`
- コミットハッシュ: (コミット後に追記)
