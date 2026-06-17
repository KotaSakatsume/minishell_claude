# 04-review.md

- **Issue:** #6 cd / export / unset(可変 env 導入)
- **Stage:** 4/5 Reviewer
- **対象:** ブランチ `feat/issue-6-builtins` / コミット `ad45acb`
- **レビュー方式:** Implementer 報告を信用せず、メインが独立に `make re`・Norm 監査・禁止構文/グローバル走査・エッジ挙動・leaks を再実行。

## 総評
**承認(差し戻し不要)。** must 級の指摘なし。可変 env 導入 + cd/export/unset の全 DoD を独立検証で確認。Norm は 42 ヘッダー(保留)を除きクリーン。should 1件(PWD乖離)+ nice 1件(未使用 env_get)は別Issue/任意。

## 独立検証
| 観点 | 結果 |
|---|---|
| `make re` ビルド | ✅ 警告/エラーなし、二度目再リンクなし、ヘッダ依存で再コンパイル |
| 関数行数(≤25) | ✅ OVER25 ゼロ |
| 1ファイル関数数(≤5) | ✅ env_utils 5 / env_set 5 / builtin_cd 3 / builtin_export 5 / builtin_unset 1 / builtins 5 |
| 禁止構文 / グローバル | ✅ なし / 0個 |
| norminette | ⚠️ `INVALID_HEADER` のみ(20ファイル) |
| leaks(env/cd/全エラー経路) | ✅ 0 leaks |

### エッジ挙動(独立実行)
- `export X=a;X=b;X=c; env` → `X=` は **1行**(上書き、重複なし)✅
- `unset NOPE` → status 0・クラッシュなし ✅
- `export ZZ`(値なし)→ no-op(env に出ない)✅ 固定判断どおり
- `export bad!=1` / `export ?bad=1` → `not a valid identifier` + status 1、env 不変 ✅
- `cd /; pwd` → `/` ✅、`cd /nope` → エラー + カレント不変 ✅
- `export PATH=/bin` → 外部コマンド解決が新 PATH を見る ✅(可変env一本化の主目的)

## 観点別レビュー

### 設計準拠
- ✅ `t_shell.env` 所有コピー、`shell_init=env_dup`/`shell_cleanup=env_free`。env builtin / PATH探索 / execve / cd・export・unset が同一 `shell->env` を参照(一本化)。
- ✅ `find_command_path`/`path_utils.c` はコード無変更で呼び出し元のみ更新(設計の3箇所切替どおり)。
- ✅ ビルトインは親実行を維持(cd の chdir/PWD 更新が親に効く)。

### バグ
- 致命・機能バグなし。
- **env_unset の前詰め**: 一致時 `i` を進めず `else i++` で再評価 → 要素飛ばしなし(調査で指摘の罠を回避)。確認済み。
- **env_set 非破壊性**: `make_kv` 失敗 → `env` をそのまま返す。`env_append` の malloc 失敗 → `free(kv)` して旧 `env` 返却。旧外枠 free は要素 shallow 移譲後のみ(二重 free なし)。leaks 0 で実証。
- **export_one のリーク安全**: 不正id は key 確保前に return、`=` なしは key 確保前に return、`ft_strndup` 失敗は env 不変で return、成功時は env_set 後に key を free。全経路で漏れなし。
- **bi_cd**: getcwd 失敗時 oldpwd="" で best-effort、chdir 失敗で status 1・カレント不変。

### セキュリティ
- ✅ エラー出力は `write` 固定文字列 + 引数のみ(フォーマット文字列注入なし)。許可関数(chdir/getcwd/malloc/free/access/execve)のみ。`setenv` 等の禁止関数不使用。

### 可読性
- ✅ env 層(env_utils/env_set)と builtin 層が分離。`make_kv`/`env_append` の抽出で各関数が短く読める。コメントが既知差分(PWD)や所有権を明示。

## 指摘ログ

### must(差し戻し対象) — なし

### should(別Issue / 非ブロッキング)
1. **cd の PWD がシンボリックリンクで bash と乖離** — `getcwd` 物理パス(`cd /tmp; pwd` = `/private/tmp`)。論理 PWD 保持は `OLDPWD+arg` 正規化が要るため別Issue。通常ディレクトリ・`cd ..` は一致。

### nice(任意)
1. **`env_get` が現状未使用** — 設計で将来の展開($VAR)/cd 拡張用に定義。`-Werror` 警告は出ない(非 static)。展開Issueで使用予定のため残置で可。気になるなら展開Issueまで削除も選択肢。
2. `export` 単独表示が `declare -x` 形式でなく env 同形(最小仕様どおり)。完全互換は export 拡張Issueで。

## 次段(Integrator)への申し送り
- 差し戻し往復: **0 回**(must なし)。段階5 へ進んでよい。
- 本ブランチは main(#2+#4 マージ済み)から分岐 → PR #6 のベースは **main** で素直。
- PR 本文に「cd/export/unset が動く」「可変env一本化」「DoD 充足表」「既知保留(42ヘッダー / PWD乖離)」を明記。`Closes #6`。
