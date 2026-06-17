# 04-review.md

- **Issue:** #4 コマンド実行(トークン化 + PATH実行 + コアビルトイン)
- **Stage:** 4/5 Reviewer
- **対象:** ブランチ `feat/issue-4-execution` / コミット `4767b2c`
- **レビュー方式:** Implementer 報告を信用せず、メインが独立に `make re`・Norm 監査・禁止構文/グローバル走査・挙動スポットチェックを再実行。

## 総評
**承認(差し戻し不要)。** must 級の指摘なし。`ls`/`pwd` を含む全 DoD を独立検証で確認。Norm は 42 ヘッダー(保留)を除きクリーン。should 3件は別Issueへ。

## 独立検証の結果
| 観点 | 結果 |
|---|---|
| `make re` ビルド | ✅ 警告/エラーなし |
| 関数行数(≤25) | ✅ OVER25 ゼロ(全関数25行以内) |
| 1ファイル関数数(≤5) | ✅ tokenize 5 / builtins 5 / builtin_exit 1 / execute 4 / path_utils 4 / process_line 2 |
| 禁止構文(for/switch/三項/do-while) | ✅ 検出ゼロ(while のみ) |
| グローバル変数 | ✅ 0 個 |
| norminette | ⚠️ `INVALID_HEADER` × 15 のみ(他違反ゼロ) |
| leaks(親 / 127経路) | ✅ 0 leaks |

### 挙動スポットチェック(独立実行)
- `echo -n abc` → `abc`(改行なし)/ `echo` → 空行 / `pwd` / `ls -d src` → `src` ✅
- `env` → 46 行出力 ✅
- 前後空白 `   pwd   ` → 正常(トークン化が空白を正しく除去)✅
- 存在しない絶対パス `/no/such/x` → `minishell: /no/such/x: command not found` ✅
- 終了コード(別実測): `ls`=0 / not found=127 / 非実行=126 / dir=126 ✅

## 観点別レビュー

### 設計準拠
- ✅ `process_line` シグネチャ維持、`tokenize→dispatch→free_argv` 構造。
- ✅ builtin は親実行(fork しない)— `dispatch` で is_builtin 真なら run_builtin。exit/将来 cd が親に効く設計を遵守(リスク2 回避)。
- ✅ PATH は `shell->envp` 走査(`getenv` 不使用)。errno→終了コードは実測どおり ENOENT→127/else→126。
- ✅ t_shell 追加なし(envp/last_status のみ)。

### バグ
- 致命・機能バグなし。所有権は明確:argv は `process_line` 末尾 or `bi_exit` で解放、path は `run_external` で解放、子は execve 失敗時 `child_exec` で `free(path)+free_argv` してから exit(リスク1 対応を目視確認)。
- `is_builtin`/`run_builtin` は `ft_strncmp(a,b,len+1)` の終端込み完全一致で誤一致なし(`echoX` 等は builtin 判定されない)。
- `search_path` の malloc 失敗時(path_join→NULL)も `if (full && access)` でスキップ・`free(NULL)` 安全・ループ前進。無限ループなし。
- `wait_status` は `WIFEXITED` ガード後に `WEXITSTATUS`、`WIFSIGNALED` で 128+sig。未定義参照なし。

### セキュリティ
- ✅ エラー出力は `write` で固定文字列 + コマンド名のみ(フォーマット文字列注入なし)。許可関数(fork/execve/waitpid/access/getcwd/getenv)のみ。

### 可読性
- ✅ ファイル分割が役割単位で明快。コメントが「なぜ親実行か」「子で free する理由」を説明しており保守しやすい。

## 指摘ログ

### must(差し戻し対象) — なし
- 〔参考〕42 ヘッダー未付与はユーザー判断の保留であり must ではない。

### should(別Issue / 非ブロッキング)
1. **`exit` builtin が非対話でも `exit\n` を出力** — bash は対話時のみ。`isatty` 導入Issueでまとめて gate(Issue#2 の should と同根)。
2. **PATH の空セグメント(`::` や先頭/末尾 `:`)を `"/cmd"` として扱う** — bash はカレントディレクトリ扱い。最小実装では非対応。クォート/展開や cd 後の挙動を扱うIssueで合わせて対応推奨。
3. **`echo` が `-n` 単独のみ対応** — `-nnn`/複数 `-n` 連結は未対応(設計上の最小仕様)。bash 完全互換は echo 拡張Issueで。

### nice
- `bi_pwd` の `cwd[4096]` 固定長 — 超長パスで getcwd 失敗時は status=1。許容範囲。将来 `getcwd(NULL,0)`(GNU拡張)or PATH_MAX 検討。

## 次段(Integrator)への申し送り
- 差し戻し往復: **0 回**(must なし)。段階5 へ進んでよい。
- PR 本文に「ls/pwd が動く」「DoD 充足表」「既知保留(42ヘッダー)」「should 3件は別Issue」を明記。`Closes #4`。
- ベースブランチ注意: 本ブランチは `feat/issue-2-foundation`(PR #3 未マージ)の上に積まれている。PR #4 のベースをどうするか(#3 を先にマージ / #2ブランチをベースに / main へ直接)は Integrator 段階でユーザー確認。
