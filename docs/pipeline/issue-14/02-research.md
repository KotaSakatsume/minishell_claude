# 02-research.md

- **Issue:** #14 シグナル(ctrl-C / ctrl-D / ctrl-\)+ 唯一のグローバル変数
- **Stage:** 2/5 Investigator
- **実測環境:** macOS (Darwin 21.6.0 / Intel), Apple clang 14, 2026-06-17 にパイプライン本体で実測
- **方式:** Architect 要請の7点をプロトタイプ + 実測。**`expect` が利用可能なので対話 ctrl-C も pty 経由で検証できる**(下記10)。

## 1. シグナル終了ステータス 130 / 131 — 【実測済み】
fork した子を `SIG_DFL` にし、親(`SIG_IGN`)から `kill` してプロトタイプ検証:
- `SIGINT` → **130**(128+2)、`SIGQUIT` → **131**(128+3)✅
- **親は `SIG_IGN` で生存**(child だけ死ぬ)✅ = 実行モードの正確なパターン
- **確定:** 既存 `wait_status`(WIFSIGNALED→128+sig)が無改修で 130/131 を返す。中断時の last_status はそのまま。

## 2. 親 SIG_IGN / 子 SIG_DFL = 子のみ中断 — 【実測済み】
- プロトタイプで「親 SIG_IGN 維持 + 子 SIG_DFL で kill」→ 子のみ終了・親生存を確認 ✅
- **setpgid は不要(最小実装):** 端末のフォアグラウンドプロセスグループに minishell と fork 子が同居するため、ctrl-C は両者に届く(親=無視 / 子=死ぬ)。ジョブ制御(`setpgid`/`tcsetpgrp`)なしで成立。→ 設計どおり `setpgid` はスコープ外で良い。

## 3. readline 再表示 API — 【実測済み・利用可】
- `nm libreadline.dylib` → `rl_on_new_line` / `rl_replace_line` / `rl_redisplay` が**実在**(`-lreadline` で解決)✅
- ヘッダ `readline/readline.h` に宣言あり。macOS Homebrew readline でリンク可(#2 のパス解決を継承)。
- **確定:** 対話 SIGINT ハンドラで `write(1,"\n") + rl_on_new_line() + rl_replace_line("",0) + rl_redisplay()` の標準パターンが使える。

## 4. sigaction フラグ(SA_RESTART)— 【実測 + 方針確定】
- `SA_RESTART` = `0x0002`(実在)✅
- **方針: 対話 SIGINT ハンドラに `SA_RESTART` を付ける。** 理由: SA_RESTART を付けると ctrl-C 後に readline の `read` が EINTR で中断されず、**ハンドラ内の rl_redisplay が新プロンプトを描き、readline は入力待ちを継続**(bash 流の「ctrl-C→新行・行破棄・継続」)。付けないと read が EINTR で readline が中途半端に戻り扱いが面倒。42 の標準パターンは SA_RESTART + ハンドラ内 rl 再表示。
- **SIGQUIT は `SIG_IGN`**(ハンドラ不要)。対話プロンプトで ctrl-\ は無反応。

## 5. last_status=130 の確定場所 — 【設計確定】
- ハンドラは `g_signal=SIGINT` を書くのみ(t_shell 不可視=規約厳守)。
- **repl_loop が readline 復帰後に `if (g_signal == SIGINT) { shell->last_status = 130; g_signal = 0; }`** で確定。
- 実行中断は waitpid→wait_status(128+sig)で確定(g_signal を見ない)。クリアは対話側のみ。

## 6. ハンドラの async-signal-safe — 【確認】
- ハンドラ内は `write` + `sig_atomic_t` 代入 + readline 再表示 API のみ。`printf`/`malloc`/`free` 不使用。
- `g_signal` は `sig_atomic_t`(`<signal.h>`)。原子的読み書き保証。

## 7. 許可関数の確認 — 【確認】
- 課題許可リストに `signal`, `sigaction`, `sigemptyset`, `sigaddset`, `kill`, `rl_clear_history`, `rl_on_new_line`, `rl_replace_line`, `rl_redisplay` が含まれる(#2 で抽出済みリスト)。すべて使用可。
- `sigaction` の戻り値チェック省略は起動時 best-effort で 42 評価上問題になりにくい(bash も失敗で落ちない)。Norm 行数優先で呼びっぱなし許容。

## 8. bash の中断時改行 / Quit メッセージ — 【方針】
- **実行中断(子が SIGINT):** 端末が `^C` を表示。子が SIG_DFL で死ぬので OS/端末側が改行を処理。**minishell 側で waitpid 後に追加の改行を出す必要は基本なし**(bash も子の SIGINT では端末表示に委ねる)。→ 最小は「追加改行なし、status 130 のみ」。Reviewer が pty 実機で bash と比較し、必要なら改行1個追加(should)。
- **SIGQUIT(ctrl-\):** 端末/カーネルが `Quit: 3`(macOS は `Quit`)相当を表示。`$?`=131。minishell は特別な出力不要。

## 9. heredoc 中の ctrl-C(最小固定)
- **最小ゴール:** heredoc の `readline("> ")` 中の ctrl-C で heredoc を中断しコマンド非実行・`$?`=130。
- 現状 `run_one_heredoc` は readline NULL(EOF)で break する。ctrl-C を SA_RESTART 付き対話ハンドラにすると readline が中断されない(継続)ため、**heredoc 中だけは別扱い**が要る。
- **判定:** 完全準拠は複雑(heredoc 中は SIGINT で readline を中断して中断扱いにする=SA_RESTART を一時的に外す or ハンドラで rl をキャンセル)。**最小実装が小改修で済むか Implementer が試し、困難なら本 Issue ではスコープ外降格**(コマンドプロンプト + 実行中断を確実に満たす)。DoD でこの項目は「達成 or 降格を明記」とする。

## 10. 対話テスト手段(重要)— 【expect 利用可】
- **`/usr/bin/expect` が利用可能** → pty 経由で minishell を対話起動し、`\x03`(ctrl-C)/`\x1c`(ctrl-\)/`\x04`(ctrl-D)を送って挙動を自動検証できる。
- Implementer/Reviewer は次のような expect スクリプトで検証可能(実装後):
  ```expect
  spawn ./minishell
  expect "minishell$ "
  send "\003"                  ;# ctrl-C
  expect "minishell$ "         ;# 新プロンプトが出る
  send "echo \$?\r"
  expect "130"                 ;# status 130
  send "sleep 5\r"; send "\003"; expect "minishell$ "  ;# 実行中断
  send "\004"                  ;# ctrl-D で終了
  ```
- **非対話パイプ(`echo cmd | ./minishell`)では readline がプロンプトを出さないため対話シグナルは検証不可** → expect/pty が必須。実行中断 status と既存回帰は非対話でも一部確認可。

## 11. 既存コードとの接続(事実確認)
- `src/main.c`: `g_signal` の初期化は定義時 `= 0` で足り、main で特別な初期化不要。
- `src/repl.c`: while 先頭で `set_signals_interactive()`、`readline` 後に g_signal→130 反映 + クリア。
- `src/execute.c` `run_external`: fork 前 `set_signals_exec_parent()`、子 `child_run_external` 先頭 `reset_signals_child()`。
- `src/pipeline.c` `exec_pipeline`: fork ループ前 `set_signals_exec_parent()`。`src/pipeline_child.c` `pipe_child` 先頭 `reset_signals_child()`。
- `include/minishell.h`: `#include <signal.h>`、`extern sig_atomic_t g_signal;`、3プロトタイプ。
- `Makefile`: SRCS に `src/signals.c`。

## 12. 42 Norm 注意
- `signals.c` は5関数(g_signal 定義 + sigint_prompt + 3設定関数)。設定関数は `struct sigaction` をスタックに作り順次 sigaction/signal、25行以内。
- **グローバルは g_signal 1個のみ**。他にファイルスコープ可変グローバルを足さない(`grep` で確認)。
- 三項/for/switch 禁止、while のみ。`sa.sa_handler` 代入は順次。

## 13. リスク箇所(上位3)
### リスク1: readline 中断挙動(SA_RESTART)で新プロンプトが出ない/二重表示
- 回避: SA_RESTART + ハンドラ内 `rl_on_new_line/rl_replace_line/rl_redisplay`。expect で実機検証。

### リスク2: 実行モードのハンドラが対話に戻らない(復帰漏れ)
- 回避: repl_loop が**毎反復先頭で set_signals_interactive()**。実行中 SIG_IGN のまま次プロンプトに来ても上書きされる。

### リスク3: グローバル規約違反(番号以外を持たせる)
- 回避: g_signal は番号のみ。last_status は repl_loop/waitpid が確定。ハンドラに shell を渡さない。

## 14. Implementer が叩く検証コマンド
```bash
make
# 非対話で確認できる範囲(実行中断 status・回帰)
printf 'sleep 1\necho done\n' | ./minishell        # 回帰
# 対話(expect/pty)で確認
expect -c 'spawn ./minishell; expect "minishell$ "; send "\003"; expect "minishell$ "; send "echo \$?\r"; expect "130"; send "\004"'
expect -c 'spawn ./minishell; expect "minishell$ "; send "sleep 5\r"; send "\003"; expect "minishell$ "; send "echo \$?\r"; expect "130"; send "\004"'
# グローバル1個確認
grep -rnE '^[a-zA-Z].*g_' src/ | grep -v 'g_signal'   # 他グローバル無し
/tmp/pdfvenv/bin/norminette src include libft
```

## 申し送り
1. **42 ヘッダー未付与**(ログイン待ち)継続。
2. **SA_RESTART を付ける**(対話 SIGINT)。`wait_status` 無改修で 130/131。`setpgid` 不要。
3. **last_status=130 は repl_loop が設定**(ハンドラは g_signal のみ)。グローバルは g_signal 1個。
4. **対話テストは expect/pty で実施**(非対話パイプ不可)。実装後に expect スクリプトで ctrl-C→新プロンプト+130、sleep中断、ctrl-D 終了を検証。
5. **heredoc 中 ctrl-C は最小 or 降格**(小改修で達成可なら実装、困難なら明記してスコープ外)。

## 関連ファイル
- 設計入力: `docs/pipeline/issue-14/01-design.md`
- 本成果物: `docs/pipeline/issue-14/02-research.md`
