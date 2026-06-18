# 01-design.md

- **Issue:** #14 シグナル(ctrl-C / ctrl-D / ctrl-\)+ 唯一のグローバル変数 `g_signal`
- **Stage:** 1/5 Architect

## 方針 (1行)
**唯一のグローバル `sig_atomic_t g_signal`(受信シグナル番号のみ)** を新ファイル `src/signals.c` に定義し、シェルは「**対話モード**(プロンプト待ち: SIGINT=新プロンプト再表示 / SIGQUIT=無視)」と「**実行モード**(fork 中: 親 SIG_IGN / 子 SIG_DFL)」の2つのハンドラ状態を `sigaction` で切り替える。`last_status` はハンドラでは絶対に触らず(t_shell 不可視)、**repl_loop が readline 復帰後に `g_signal` を見て 130 を立てる**、実行中断は既存 `wait_status` の 128+sig がそのまま 130/131 を返す。

## 設計方針 (5-7行)
- **アーキテクチャ:** ハンドラ2系統。(A)対話=`sigint_prompt`(SIGINT で `g_signal=SIGINT`、`write(1,"\n")`+`rl_on_new_line`+`rl_replace_line("",0)`+`rl_redisplay`)/ SIGQUIT=`SIG_IGN`。(B)実行=親で SIGINT/SIGQUIT を `SIG_IGN`、子(fork 後・execve/builtin 前)で `SIG_DFL` に戻す。`set_signals_interactive()` / `set_signals_exec_parent()` / `reset_signals_child()` の3関数で状態遷移。
- **データフロー:** `main → shell_init` で `g_signal=0`。`repl_loop`: 各反復先頭で `set_signals_interactive()` → `readline` → 復帰後 `if (g_signal==SIGINT){ shell->last_status=130; g_signal=0; }` → process_line。`run_external`/`exec_pipeline`: fork ループ前後で親を `SIG_IGN`↔対話に切替、子は `reset_signals_child()` 後に exec。
- **主要インターフェース:** `extern sig_atomic_t g_signal;`(ヘッダ宣言、`signals.c` で定義)。`void set_signals_interactive(void); void set_signals_exec_parent(void); void reset_signals_child(void);`。t_shell は一切渡さない(番号のみ規約)。
- **DB / スキーマ変更:** なし(該当しない)。
- **エラーハンドリング:** ハンドラ内は **async-signal-safe な `write` のみ**(printf/malloc/rl_* のうち rl_redisplay 等は readline が signal-safe 用途で提供する API なので対話ハンドラ内でのみ使用、Investigator が裏取り)。`sigaction` 失敗は起動時 1 回設定なので致命扱いせず無視可(bash 同等の best-effort)。中断後の `last_status` 確定は repl_loop(対話)/ waitpid(実行)に集約し、ハンドラに状態書込みを持たせない。

## 採用理由とトレードオフ
- **採用:** ハンドラは `g_signal` に番号を書くだけ+対話再表示の write/rl のみ。`last_status` は repl_loop と waitpid が確定。理由: ハンドラから t_shell に触れない=**グローバル1個(番号のみ)の Norm を構造的に守れ**、async-signal-safe 違反も避けられる。
- **却下A:** グローバルを `t_shell*` か `int last_status` の構造体にしてハンドラで 130 を直書き。理由: **42 規約「グローバルは受信番号のみ・データアクセス禁止」に直接違反**。番号のみ方式なら repl_loop が後付けで status を立てられ要件を満たす。
- **却下B:** `signal()` 一本で全部済ませる(sigaction を使わない)。理由: `signal` は SA フラグ(SA_RESTART の有無=readline を EINTR で戻すか)を制御できず移植性も低い。**`sigaction` でフラグを明示**し、readline 中断挙動を bash 流に固定する方が安全(Investigator が SA_RESTART の要否を裏取り)。

---

## 1. 設計方針 — g_signal の最小設計とハンドラ状態
### 1-1. 状態機械(2モード)
| モード | 設置タイミング | SIGINT | SIGQUIT |
|---|---|---|---|
| 対話(プロンプト待ち) | repl_loop の `readline` 前 | `sigint_prompt`(番号セット+新プロンプト) | `SIG_IGN`(完全無視) |
| 実行中・親 | fork ループ前(run_external / exec_pipeline) | `SIG_IGN`(親は死なない) | `SIG_IGN`(親は死なない) |
| 実行中・子 | fork 後・execve/builtin 前 | `SIG_DFL`(ctrl-C で死ぬ→130) | `SIG_DFL`(ctrl-\ で死ぬ→131) |

- 実行終了(waitpid 完了)後、**親は対話モードへ復帰**(run_external / exec_pipeline の末尾、または process_line 復帰直後の repl_loop で再設定)。本設計では **repl_loop が毎反復先頭で `set_signals_interactive()` を呼ぶ**ことで、実行モードに切り替えたままでも次プロンプトで必ず対話に戻る(復帰漏れを構造的に防止)。
- `g_signal` の型は **`sig_atomic_t`**(`<signal.h>`。async-signal 文脈での原子的読み書き保証)。初期値 0、用途は「直近に受けたシグナル番号」のみ。

### 1-2. ファイル分割(新ファイル `src/signals.c`、Norm 5関数/file)
| ファイル | 関数(≤5) | 役割 |
|---|---|---|
| `src/signals.c` | `g_signal` 定義 / `sigint_prompt`(ハンドラ) / `set_signals_interactive` / `set_signals_exec_parent` / `reset_signals_child` | シグナル設定の中枢 |
- 5関数を超える場合のみ `src/signals_exec.c` に親/子切替を分離(Investigator が行数で判定。現状は1ファイルで収まる見込み)。
- `Makefile` の `SRCS` に `src/signals.c` を追加。ヘッダに `extern` 宣言+3プロトタイプ追加。

---

## 2. グローバル変数の定義/宣言と「番号のみ」厳守
- **定義(1箇所のみ):** `src/signals.c` 先頭に `sig_atomic_t g_signal = 0;`。
- **宣言:** `include/minishell.h` に `# include <signal.h>` と `extern sig_atomic_t g_signal;`(または `signals.c` のプロトタイプ群と同じ場所)。
- **禁止事項(Norm/規約):**
  - `g_signal` に **t_shell ポインタや last_status を持たせない**。番号(int 相当)のみ。
  - ハンドラ内から `shell->last_status` を書かない(そもそも shell 不可視)。
  - グローバルはこの1個だけ。他のファイルスコープ可変グローバルを増やさない(`static const` テーブル等は不可侵だが本 Issue では不要)。
- **last_status=130 の確定場所(必読):** ハンドラは番号を入れるだけ。**repl_loop が `readline` 復帰後に `g_signal` を読み、SIGINT なら `shell->last_status=130` をセットし `g_signal=0` でクリア**する。これにより「ハンドラ→グローバル番号→repl_loop が status 反映」の一方向データフローになり、規約と async-safe を両立。

---

## 3. 対話ハンドラ(SIGINT 再表示 / SIGQUIT 無視)
### 3-1. `sigint_prompt`(ハンドラ本体、async-signal-safe)
```
void sigint_prompt(int sig)
{
    g_signal = sig;                 /* sig_atomic_t への書込みのみ */
    write(STDOUT_FILENO, "\n", 1);  /* bash: ctrl-C で改行 */
    rl_on_new_line();               /* readline に「新しい行頭」を通知 */
    rl_replace_line("", 0);         /* 入力中バッファを破棄(行破棄要件) */
    rl_redisplay();                 /* 新プロンプトを再描画 */
}
```
- `printf` / `malloc` は使わない。`rl_on_new_line` / `rl_replace_line` / `rl_redisplay` は **許可関数**かつ readline がシグナル再表示用に提供する API。**ただし「ハンドラ内で rl_redisplay を呼ぶ正攻法」か「ハンドラは g_signal だけ立て、repl_loop が rl を呼ぶ」かは挙動差があるため Investigator が実機裏取り**(末尾依頼参照)。デフォルト方針は上記ハンドラ内再表示(42 で一般的・許可関数で完結)。
- SIGQUIT は `SIG_IGN`(ハンドラ不要)。対話プロンプトで ctrl-\ は**何も起きない**(bash 同等)。

### 3-2. sigaction の設定(`set_signals_interactive`)
```
set_signals_interactive():
    sa.sa_handler = sigint_prompt;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;        /* ← Investigator 裏取りで確定(5節参照) */
    sigaction(SIGINT,  &sa, NULL);
    signal(SIGQUIT, SIG_IGN);         /* or sigaction で SIG_IGN */
```
- **設置タイミング:** repl_loop の **`readline("minishell$ ")` を呼ぶ直前**(各反復先頭)。実行から戻った直後も毎反復先頭で再設定されるため、実行モードのまま残らない。

---

## 4. 実行中のハンドラ切替(親 SIG_IGN / 子 SIG_DFL)
### 4-1. なぜ親 SIG_IGN・子 SIG_DFL か
- **親が SIG_IGN にする理由:** ctrl-C は端末のフォアグラウンドプロセスグループ全員に届く。親(シェル)が SIG_DFL のままだと**シェル自身が死ぬ**。`SIG_IGN` にすることで **ctrl-C は実行中の子だけに効き、シェルは生存**(bash 同等)。SIGQUIT も同様(シェルは ctrl-\ で落ちない)。
- **子が SIG_DFL に戻す理由:** 親から継承した `SIG_IGN` のままだと、実行中コマンドが **ctrl-C で死なない**。fork 直後・execve 前に `SIG_DFL` へ戻すことで、`sleep 100` 等が ctrl-C(→130)/ ctrl-\(→131)で正しく終了する。

### 4-2. 設置箇所(execute.c と pipeline.c 両方)
- **`src/execute.c` `run_external`:**
  - `fork()` の **直前**に `set_signals_exec_parent()`(親を SIGINT/SIGQUIT=SIG_IGN)。
  - 子側 `child_run_external` の **先頭**(apply_redirs / execve の前)に `reset_signals_child()`(SIGINT/SIGQUIT=SIG_DFL)。
  - `waitpid` 後は **repl_loop が次反復で対話に戻す**ため、ここでの明示復帰は必須ではない(が、process_line から戻るまでに別の対話操作が無いので不要)。
- **`src/pipeline.c` `exec_pipeline` / `pipe_fork_one`:**
  - fork ループ全体の **前**に1回 `set_signals_exec_parent()`(各 `pipe_fork_one` で毎回呼ぶ必要はなく、ループ前1回で親は全 fork 期間 SIG_IGN)。
  - 各子=`pipe_child`(`src/pipeline_child.c`)の **先頭**(`pipe_dup` の前 or 直後、heredoc/redir/execve の前)に `reset_signals_child()`。
- **builtin の親実行(process_line の exec_builtin_redir 等):** fork しないため対話ハンドラのまま。ctrl-C で builtin が走っている瞬間は短く、bash も親 builtin 中の ctrl-C は基本シェルに影響しない範囲。**本 Issue は「対話ハンドラのまま=SIGINT で新プロンプト」で最小固定**(builtin 実行中の厳密 ctrl-C はスコープ外)。

### 4-3. 子の reset 位置の注意
- `reset_signals_child()` は **fork 後・子だけが通る経路**(`child_run_external` 先頭 / `pipe_child` 先頭)で呼ぶ。親は SIG_IGN を維持。子が builtin を実行して exit する経路(パイプ内 builtin)でも先頭で reset 済みなので問題なし。

---

## 5. 中断後の改行 / status(130 / 131 / WIFSIGNALED)
- **実行中断:** 子が SIGINT で死ぬ → `wait_status`(既存 `execute.c`)が `WIFSIGNALED` → `128 + WTERMSIG = 128+2 = 130`。ctrl-\ → `128+3 = 131`。**`wait_status` は無改修で 130/131 を返す**(`shell->last_status` に格納済み)。
- **bash の改行挙動:** 子が SIGINT で死んだとき、bash は `^C` の後に改行を出す。子が `SIG_DFL` で死ぬと端末が `^C` を表示し、bash 側も整形改行を出す。**本設計では「子の SIG_DFL による終了 → 端末が `^C` を表示、シェルは waitpid 後 WIFSIGNALED が SIGINT のとき改行を1つ write する」か「改行不要(端末出力で足りる)」かを Investigator が実機で確認・固定**(末尾依頼。SIGINT のみ改行、SIGQUIT は `Quit: 3` 系メッセージを端末/シェルどちらが出すかも確認)。
- **対話プロンプトでの ctrl-C:** これは実行中ではなくプロンプト待ち。ハンドラが改行+新プロンプトを出し、repl_loop が `last_status=130`。
- **フォアグラウンドプロセスグループ:** 親が SIG_IGN 中に押された ctrl-C は、端末のフォアグラウンドプロセスグループ経由で **子に届く**(子が SIG_DFL なので死ぬ)。親はジョブ制御未実装でも、fork 子が同一プロセスグループ・前面にいるため成立する点を Investigator が裏取り(`setpgid` が必要かどうか。bash はジョブ制御で `setpgid` するが、本 Issue は**ジョブ制御なしで子が前面 PG にいる前提**で最小実装。`setpgid` 追加が必要なら後続)。

---

## 6. heredoc 中のシグナル(最小固定)
- **方針(最小・1つに固定):** heredoc の `readline("> ")` ループ中の ctrl-C は **「ヒアドキュメント入力を中断し、コマンド全体を非実行にする(`last_status=130`)」を最小ゴール**とする。bash 完全準拠(EOF 扱い・部分入力破棄の細部)はスコープ外。
- **実装の方向性(設計のみ):** heredoc 実行中も子で読む(`pipe_child` 内)/ 親 process_line で読む(`exec_cmd→run_heredocs`)経路がある。**最小実装としては:heredoc 中は SIGINT で readline が中断され(SA_RESTART を付けない or EINTR で readline が NULL を返す)、`run_one_heredoc` が中断を検知して heredoc を打ち切り、`run_heredocs` がエラーを返して非実行**にする。`g_signal==SIGINT` を見て打ち切る形が素直。
- **ただし本 Issue のスコープは「heredoc 中 ctrl-C の bash 完全準拠は対象外(最小 or 後続)」**と明記済み。**最小=「ctrl-C で heredoc 中断・行/コマンド破棄・130」**を1つだけ満たし、`^C` エコー抑制(termios)や heredoc 続行不可の細部は後続 Issue。Investigator が「readline("> ") 中の ctrl-C が現状どう振る舞うか(NULL 返却 / 再描画)」を実機確認し、最小で達成可能かを判定(達成困難なら**現状維持=heredoc 中 ctrl-C はスコープ外に降格**し、コマンドプロンプトと実行中のみ本 Issue で確実に満たす)。

---

## 7. メモリ / Norm(グローバル1個 / async-signal-safe / リーク無し)
1. **グローバルは `g_signal` ただ1個**(`src/signals.c` 定義、`extern` 宣言)。他にファイルスコープ可変グローバルを増やさない。
2. **ハンドラ内は async-signal-safe:** `write` と `sig_atomic_t` 代入のみを基本とし、対話再表示の `rl_on_new_line / rl_replace_line / rl_redisplay` は readline 提供 API として使用(Investigator 裏取り)。`printf` / `malloc` / `free` をハンドラ内で呼ばない。
3. **メモリリーク無し:** シグナル機能はグローバル番号と sigaction 構造体(スタック)のみで**動的確保なし**。既存の readline 行 free / env free / rl_clear_history を壊さない。中断時も親プロセスの解放経路は process_line / repl_loop の既存 free を通る(子は exit で OS 回収)。
4. **Norm:** 25行/関数・5関数/file・80列・`while` のみ(for/三項/switch 禁止)・`sigaction` 設定は分岐を `while`/早期 return ではなく順次実行で 25 行内に収める。`signals.c` は5関数(定義含む)で構成。
5. **再リンク無し:** `Makefile` 依存(`include/minishell.h` 依存ルール)を維持し、`make` 後 `make` で再ビルドが走らないこと。

---

## 8. エラー / スコープ / やらないこと / 後続
### 8-1. エラーハンドリング
- `sigaction` / `signal` の失敗: 起動・実行切替の best-effort。致命扱いせず続行(bash も signal 設定失敗で落ちない)。戻り値チェックは Norm 行数と相談し、最小では呼びっぱなしを許容(Investigator が 42 評価で減点されないか確認)。
- `g_signal` のクリア漏れ防止: **repl_loop が SIGINT 反映後に必ず `g_signal=0`** にする。実行中断は waitpid 経由で status が確定するため `g_signal` を見ない(クリアは対話側のみで一貫)。

### 8-2. スコープ(影響範囲 / 想定変更行のオーダー)
| 対象 | 変更内容 | オーダー |
|---|---|---|
| `include/minishell.h` | `#include <signal.h>` / `extern sig_atomic_t g_signal;` / 3プロト | +5 行 |
| `src/signals.c`(新) | g_signal 定義 / ハンドラ / interactive / exec_parent / child | +60〜80 行 |
| `src/repl.c` | 反復先頭で `set_signals_interactive()` / readline 後に g_signal→130 | +6 行 |
| `src/execute.c` | run_external: fork 前 exec_parent / child 先頭 reset | +4 行 |
| `src/pipeline.c` | exec_pipeline: fork ループ前 exec_parent | +2 行 |
| `src/pipeline_child.c` | pipe_child 先頭で reset_signals_child | +2 行 |
| `src/heredoc.c`(最小なら) | run_one_heredoc に ctrl-C 中断検知 | +4〜8 行 |
| `Makefile` | SRCS に signals.c | +1 行 |
- **合計 ~85〜110 行 / 1 PR で完結するサイズ。** heredoc の ctrl-C 厳密対応が膨らむ場合は「対話+実行のシグナル」と「heredoc ctrl-C」を別 PR に分割提案(heredoc は元々スコープ最小)。

### 8-3. やらないこと(3点)
1. **ジョブ制御 / ctrl-Z(SIGTSTP)/ `setpgid` によるプロセスグループ制御 / fg・bg** — 完全に別 Issue。本 Issue は「子が前面 PG にいる前提」の最小実装。
2. **`^C` エコー抑制(termios の `ECHOCTL` 操作)/ ターミナル属性のsave/restore** — 端末が `^C` を表示する挙動はそのまま許容。termios 制御はスコープ外。
3. **heredoc 中 ctrl-C の bash 完全準拠**(部分入力の扱い・heredoc の EOF 警告・複数 heredoc 途中中断の厳密挙動)/ `&&` `||` / サブシェルでのシグナル — 後続。本 Issue は heredoc は最小1ケースのみ(または現状維持に降格)。

### 8-4. 後続(見通し)
- ジョブ制御 Issue で `setpgid` + `tcsetpgrp` + SIGTSTP/SIGCONT を導入する際、本 Issue の `set_signals_exec_parent/reset_signals_child` の枠をそのまま拡張(子で SIGTSTP=SIG_DFL 等)。
- heredoc ctrl-C の厳密化、`^C` エコー抑制(termios)は別 Issue で termios の save/restore とセットで実装。

---

## 9. 完了条件(DoD)— Implementer / Reviewer チェックリスト
- [ ] **空プロンプトで ctrl-C:** 改行+新しい `minishell$ ` プロンプトが出る。入力途中の文字は破棄。直後 `echo $?` が **130**。
- [ ] **入力途中で ctrl-C:** タイプ中の行が破棄され、新プロンプトに戻る(`rl_replace_line("",0)` 効果)。
- [ ] **実行中(`sleep 5`)に ctrl-C:** 子が即終了しシェルは生存、次プロンプトが出る。`echo $?` が **130**。
- [ ] **実行中に ctrl-\(SIGQUIT):** 子が Quit で終了(端末/シェルの Quit 表示)、`echo $?` が **131**。シェルは生存。
- [ ] **対話プロンプトで ctrl-\:** **何も起きない**(SIG_IGN。新プロンプトも出さない or bash 同等で無反応)。
- [ ] **ctrl-D(EOF):** 既存どおり `exit\n` を出して終了(回帰)。
- [ ] **パイプライン中(`sleep 5 | cat`)に ctrl-C:** 子群が終了しシェル生存、`$?`=130(最終段の WIFSIGNALED)。
- [ ] **既存回帰:** 通常コマンド実行 / パイプ / リダイレクト(`> >> < <<`)/ heredoc / builtin(cd/export/unset/exit)/ クォート・展開 が #4〜#12 のまま動く。
- [ ] **heredoc 中 ctrl-C(最小):** ctrl-C で heredoc 入力が中断され、コマンド非実行・`$?`=130(達成困難なら本項目はスコープ外に降格し、その旨を Reviewer に明記)。
- [ ] **グローバルは `g_signal` ただ1個**(`grep` でファイルスコープ可変グローバルが他に無いことを確認)。`g_signal` は番号のみ(t_shell/last_status を持たない)。
- [ ] **last_status は repl_loop / waitpid が確定**(ハンドラが shell に触れていない)。
- [ ] **async-signal-safe:** ハンドラ内に printf/malloc/free が無い(write と rl_* と sig_atomic_t 代入のみ)。
- [ ] **リーク無し**(leaks/valgrind で親プロセス0。シグナル機能は動的確保なし)。
- [ ] **Norm 準拠**(25行/5関数/80列/while のみ/for・三項・switch 無し/グローバル1個)。
- [ ] **再リンク無し**(`make` 後 `make` でビルドが走らない=依存正)。

---

## 後続 Investigator(Stage 2)への裏取り依頼
以下を **実コード / 実機 bash・minishell** で確認し、設計の前提を確定させること(設計ではなく事実確認):

1. **sigaction フラグ(SA_RESTART)と readline の中断挙動:** 対話 SIGINT ハンドラに `SA_RESTART` を **付ける/付けない**で readline の挙動がどう変わるか実機確認。付けると readline が EINTR で中断されず再表示が効きにくい、付けないと EINTR で readline が NULL or 行を返す等。**bash 流(プロンプトに新行を出して入力継続・行破棄)に合うフラグ**を確定。あわせて、ハンドラ内 `rl_redisplay` 方式と「ハンドラは g_signal だけ立て repl_loop が rl_* を呼ぶ」方式のどちらが安定か実機で比較。
2. **親 SIG_IGN / 子 SIG_DFL で `sleep` 中 ctrl-C が「子のみ」を殺すか:** `sleep 100` 実行中に ctrl-C → 子だけ死にシェル生存・`$?`=130 になるか。**`setpgid` 無し(ジョブ制御なし)で端末のフォアグラウンドプロセスグループ経由で子に届くか**を実機検証。届かない/シェルも死ぬ場合は `setpgid` の要否を判定(必要なら後続 Issue へ切り出し)。
3. **ctrl-C 後の `last_status=130` と改行:** (a)対話プロンプト ctrl-C 後 `echo $?`=130 になるか(repl_loop の g_signal→130 反映が正しいか)。(b)実行中断時 `wait_status` の 128+2=130 がそのまま入るか。(c)**bash が SIGINT 中断時に改行を出すか**を実機確認し、minishell が waitpid 後に改行を1つ write すべきか/不要か(端末の `^C` 表示で足りるか)を固定。
4. **ctrl-\(SIGQUIT)の Quit メッセージ / 131:** 実行中 ctrl-\ で子が Quit する際、`Quit: 3` 等のメッセージを **端末が出すかシェルが出すか**、`$?`=131 になるか。対話プロンプトでの ctrl-\ が完全無反応(SIG_IGN)で良いか bash と比較。
5. **対話と非対話の違い:** 本設計の対話シグナル(プロンプト再表示)は **非対話パイプ入力(`echo cmd | ./minishell`)では完全テストできない**(readline がプロンプトを出さない)。Reviewer/Implementer は **対話端末での手動確認が必須**である点を明記。非対話で検証可能なのは「実行中断の status(130/131)」「既存回帰」までに留まる。
6. **heredoc("> ")中 ctrl-C の現状挙動:** `readline("> ")` ループ中に ctrl-C が来たとき現状どうなるか(NULL 返却 / 行継続)。最小ゴール「heredoc 中断・コマンド非実行・130」が小改修で達成可能か、困難なら**現状維持(スコープ外降格)**で良いかを判定。
7. **許可関数の確認:** `rl_on_new_line / rl_replace_line / rl_redisplay / rl_clear_history` が課題の許可関数リストに含まれ、リンク(`-lreadline`)で解決するか(Makefile の readline パス解決が macOS/42 校内 Linux 双方で通るか)。`sigaction` 設定の戻り値チェック省略が Norm/評価で減点されないかも確認。
