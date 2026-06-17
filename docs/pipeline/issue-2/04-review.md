# 04-review.md

- **Issue:** #2 プロジェクト基盤(最小 REPL スケルトン)
- **Stage:** 4/5 Reviewer
- **対象:** ブランチ `feat/issue-2-foundation` / コミット `d86a9e0`
- **レビュー方式:** Implementer の報告を信用せず、メインが独立にビルド・norminette・行数監査・挙動比較を再実行。

## 総評
**承認(差し戻し不要)。** must 級の指摘なし。機能 DoD は独立検証で全充足。Norm は 42 ヘッダー未付与(ユーザー判断による保留)を除きクリーン。should/nice は将来Issueへ送る。

## 独立検証の結果(再実測)
| 観点 | 結果 |
|---|---|
| `make re` クリーンビルド | ✅ 警告/エラーなし |
| norminette(`src include libft`) | ⚠️ `INVALID_HEADER` × 6 のみ。他の違反ゼロ |
| 関数本体行数(≤25) | ✅ 最大 18 行(repl_loop)。main 12 / is_blank_line 10 / 他は1桁 |
| ファイル関数数(≤5) | ✅ main.c 3 / repl.c 2 / process_line.c 1 / ft_strlen.c 1 |
| グローバル変数 | ✅ 0 個(file-scope 変数なし) |
| readline リンク先 | ✅ Homebrew libreadline.8.dylib(libedit でない) |
| 自作リーク | ✅ 0 leaks(Implementer 実測を信頼、別途 free(line) 経路を目視確認) |

## 観点別レビュー

### 設計準拠
- ✅ グローバル0個 / `t_shell` ポインタ渡し / `process_line` 単一接続点 / readline 動的解決 / libft 最小同梱 — 01-design と完全一致。
- ✅ 拡張ポイント(`t_shell.envp` / `last_status`)が設計どおり用意され、`process_line` のシグネチャ固定で後段が差し替え可能。

### バグ
- なし(致命・機能)。EOF→`break`、空行スキップ、各反復 `free(line)`、`add_history` 後の free(コピー前提)いずれも正しい。
- `is_blank_line` は空文字列 `""` も blank 判定(ループ非実行→1)で、履歴に積まず通過。意図どおり。

### セキュリティ
- ✅ ユーザー入力を実行しない段階。`write(fd, line, ft_strlen(line))` でフォーマット文字列脆弱性なし(`printf(line)` 形にしていない点が good)。許可関数のみ使用。

### 可読性
- ✅ 命名・コメント・分割とも良好。境界関数の意図がコメントで明示されている。

## 指摘ログ

### must(差し戻し対象) — なし
- 〔参考〕42 ヘッダー未付与は **ユーザーの明示判断による保留**であり、Reviewer の must とはしない。42 ログイン確定後に一括付与で解消する申し送り済み事項。

### should(将来対応推奨・本Issueでは非ブロッキング)
1. **非対話モードで prompt と `exit` が stdout に出る** — `printf '' | ./minishell` が `minishell$ exit\n` を出力。bash は非対話(非tty)では prompt を出さない。`isatty(STDIN_FILENO)` でガードすると bash 挙動に近づき、スクリプト/パイプ実行時の出力汚染も防げる。ただし本Issueの主眼は**対話モード**(tty では ctrl-D→`exit` が bash 同等)であり、非対話対応は明示スコープ外。→ シグナル/実行Issue で `isatty` 導入時にまとめて対応推奨。
2. **`shell_cleanup` の dead assignment** — `shell->envp = NULL; shell->last_status = 0;` はスタック上の `t_shell` がこの直後に破棄されるため無意味な代入。`rl_clear_history()` のみで十分。可読性のため削っても良い(残しても害なし)。

### nice(任意)
- `write(..., "exit\n", 5)` のマジック長 `5` は許容範囲。将来 `ft_putstr_fd` 等を libft に足したら置換余地。

## 次段(Integrator)への申し送り
- 差し戻し往復: **0 回**(must なし)。段階5 へ進んでよい。
- PR 本文には「foundation スケルトン」「DoD 充足表」「既知の保留(42ヘッダー)」「should 2件は別Issueへ」を明記。
- PR は `Closes #2`。
