# 03-implementation.md

- **Issue:** #6 cd / export / unset(可変 env 導入)
- **Stage:** 3/5 Implementer
- **Branch:** `feat/issue-6-builtins`(main = #2+#4 マージ済みの上に構築)
- **実行者:** パイプライン本体(サブエージェント側 Bash 拒否のためメインが直接実行)

## 作成 / 変更ファイル
| ファイル | 種別 | 役割 |
|---|---|---|
| `include/minishell.h` | 変更 | `t_shell.envp` → `char **env`(所有コピー)。env/cd/export/unset プロトタイプ追加。`find_command_path(char **env,...)` に改名 |
| `src/main.c` | 変更 | `shell_init` = `env_dup`、`shell_cleanup` = `env_free` |
| `src/builtins.c` | 変更 | `is_builtin`/`run_builtin` に cd/export/unset 追加、`bi_env` を `shell->env` |
| `src/execute.c` | 変更 | PATH探索・execve を `shell->env` に切替(2箇所) |
| `src/env_utils.c` | 新規 | env_count / env_dup / env_get / env_free / env_unset(5) |
| `src/env_set.c` | 新規 | cpy / make_kv / env_find / env_append / env_set(5) |
| `src/builtin_cd.c` | 新規 | cd_error / cd_update_pwd / bi_cd(3) |
| `src/builtin_export.c` | 新規 | is_id_char / is_valid_id / print_env_plain / export_one / bi_export(5) |
| `src/builtin_unset.c` | 新規 | bi_unset(1) |
| `Makefile` | 変更 | SRCS に新規5ファイルを明示追記 |

## 設計からの逸脱
- **ファイル分割のみ微調整**(機能・方針は設計どおり): Architect 案の `env_utils.c`+`env_set.c` の関数配置を、Norm 5関数/ファイルに収めるため env_unset を `env_utils.c` 側へ、env_set のヘルパ(cpy/make_kv/env_find/env_append)を `env_set.c` 側へ配置。各ファイルちょうど5関数以内。
- **Implementer が固定した2判断**(設計の指示どおり1つに固定):
  - 引数なし `cd` → **no-op で status 0**(HOME 移動は別Issue)
  - 値なし `export NAME`(`=` なし)→ **何もしない**(env に出さない)

## 検証(すべてメイン Bash で実測 / Intel Mac, Apple clang 14, GNU Make 3.81)

### ビルド
- `make re` → 警告/エラーなし
- `make`(二度目) → `Nothing to be done for 'all'.`(再リンクなし)
- `touch include/minishell.h && make` → 14 ファイル再コンパイル(ヘッダ依存が効く)

### 機能(非対話)
| 操作 | 結果 |
|---|---|
| `cd /tmp; pwd` | `/private/tmp`(getcwd 物理パス。下記既知差分)✅ |
| `cd ..; pwd` | 親ディレクトリに反映 ✅ |
| `export A=1; env` → `export A=2; env` | `A=1` → `A=2`(**上書き・重複なし**)✅ |
| `unset A; env` | `A` 消滅 ✅ |
| `export PATH=/bin; ls -d /etc` | `/etc`(**新 PATH で解決**)✅ |
| `export PATH=/bin; nosuchtool` | `command not found`(狭めた PATH 反映)✅ |
| `export ?bad=1` | `minishell: export: \`?bad=1': not a valid identifier` + status 1 ✅ |
| `cd /nonexistent_xyz` | `minishell: cd: ...: No such file or directory` + カレント不変 ✅ |
| 回帰 `ls`/`echo -n`/`env`/`exit` | 従来どおり ✅ |

### Norm
- `norminette src include libft` → エラー種別 **`INVALID_HEADER` のみ(20ファイル)**。
- 自己監査: 全関数 ≤25 行(OVER25 ゼロ)、新規各ファイル ≤5 関数、`builtins.c` 5関数維持、while のみ・グローバル0個。

### リーク
- `MallocStackLogging=1 leaks --atExit`(入力: export 追加/上書き、unset、不正id、cd /tmp、cd ..、env)→ **`0 leaks for 0 total leaked bytes`** ✅
- env_dup / env_set(追加=旧外枠free+要素shallow移譲 / 上書き=旧要素free)/ env_unset(前詰め)/ export 一時 key の free をカバー。

## Reviewer への申し送り
1. **【既知・受容】42 ヘッダー未付与** — `INVALID_HEADER` のみ。ユーザー判断で保留。must 対象外。
2. **【既知差分】cd の PWD がシンボリックリンクで bash と乖離** — `getcwd` が物理パスを返すため `cd /tmp; pwd` = `/private/tmp`(bash は論理 `/tmp`)。最小実装の割り切り。should(論理PWD保持は別Issue)。
3. **env_unset の前詰めループ** — 削除時は `i` を進めず再評価(`else i++`)で要素飛ばしを回避。Reviewer 確認推奨(調査の罠)。
4. **env_set 非破壊性** — make_kv/env_append の malloc 失敗時に旧 env を壊さず返す順序にしてある。
5. **2判断固定**(no-arg cd = no-op / 値なし export = no-op)を DoD に反映済み。

## ブランチ / コミット
- ブランチ: `feat/issue-6-builtins`
- コミットハッシュ: (コミット後に追記)
