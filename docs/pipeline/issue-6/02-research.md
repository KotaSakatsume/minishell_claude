# 02-research.md

- **Issue:** #6 cd / export / unset(可変 env 導入)
- **Stage:** 2/5 Investigator
- **実測環境:** macOS (Darwin 21.6.0 / Intel), Apple clang 14, 2026-06-17 にパイプライン本体 Bash で実測
- **方式:** Architect が要請した2点(env_set/unset の再確保リーク有無 / cd 後の PWD 整合)を /tmp のプロトタイプ実コードで裏取り。

## 1. env_set / env_unset / env_dup の再確保 — 【実測済み・リーク0】

設計どおりの env ヘルパ(env_dup / env_set〔追加・上書き〕/ env_unset / env_free)を最小プロトタイプで実装し `-Wall -Wextra -Werror` でビルド・実行・`leaks`。

| 検証 | 期待 | 実測 |
|---|---|---|
| `env_dup(envp)` の要素数 | 継承 env と同数 | 44 ✅ |
| `env_set("FOO","1")`(新規追加) | count+1 | 45 ✅ |
| `env_set("FOO","2")`(同名上書き) | `FOO=` は1行のみ・値が 2 | occurrences=1, `FOO=2` ✅ |
| `env_unset("FOO")` | FOO 消滅・count 戻る | 0 occ, count=44 ✅ |
| `leaks --atExit` | 0 | **`0 leaks for 0 total leaked bytes`** ✅ |

### 確定: 再確保パターンは安全
- **新規追加:** `count+2` の新外枠を確保 → 旧要素を **shallow コピー(ポインタ移譲)** → 末尾に新 `"key=val"` + NULL → **旧外枠のみ free**(要素は二重 free しない)。leaks 0 で実証。
- **同名上書き:** 先に新 `"key=val"` を malloc 成功 → 旧要素 free → 差し替え(配列長不変)。malloc 失敗時は旧 env を無傷で返す(非破壊)順序にする。
- **unset:** 該当要素 free → 以降を前詰め(`while(env[j]){env[j]=env[j+1];j++;}`)→ 末尾 NULL。外枠の縮小再確保は不要(cleanup で一括解放)。leaks 0。
- **実装上の注意(プロトタイプで踏んだ罠):** unset の前詰めループ後は **`continue` で同じ i を再評価**しないと、詰めた直後の要素を飛ばす。設計の env_unset 実装時にこのループ制御を Reviewer が確認すること。

## 2. cd 後の PWD 整合 — 【実測・bash 乖離を発見】

`getcwd` の挙動を実測:
```
cwd before       = /private/tmp/inv6
chdir("/tmp")    -> 成功
getcwd after     = /private/tmp        ← /tmp ではない!
```

### 重要な発見: getcwd はシンボリックリンクを解決する(物理パス)
- macOS では `/tmp` は `/private/tmp` へのシンボリックリンク。`cd /tmp` 後に `getcwd` で PWD を組むと **`PWD=/private/tmp`** になる。
- **bash は論理 PWD を保持**するため `cd /tmp; pwd` → `/tmp`(リンク解決しない)。
- **含意:** 設計の「chdir → getcwd → env_set("PWD")」は **シンボリックリンク配下で bash と PWD 値が乖離**する。
- **最小実装の判断:**
  - (推奨・最小) **getcwd ベースで PWD を更新**(物理パス)。実装が単純で許可関数のみ。シンボリックリンク非配下では bash と一致。リンク配下の乖離は **should/既知差分**として申し送る。
  - (上位・別Issue) 論理 PWD を `OLDPWD + "/" + arg` の正規化で構築する方式。複雑なので本Issueでは採らない。
- **getcwd 失敗時(削除済みディレクトリ等):** chdir 成功なら last_status=0 を維持し、PWD 更新は best-effort(env_set をスキップ or 旧値据え置き)。クラッシュさせない。本Issueの DoD は「通常パスでの cd /tmp→pwd 反映」を主眼にし、削除済みディレクトリの厳密 bash 準拠はスコープ外でよい。

## 3. 既存コードとの接続(事実確認)
- `include/minishell.h` の `t_shell` は `char **envp; int last_status;`。`envp` を `env`(所有コピー)に置換 or 追加。
- `src/main.c` `shell_init` は現状 `shell->envp = envp;`(参照のみ)→ `shell->env = env_dup(envp);` に。`shell_cleanup` に `env_free` 追加。
- `src/builtins.c` `bi_env` は `shell->envp[i]` 走査 → `shell->env[i]`。
- `src/execute.c` `run_external` は `find_command_path(shell->envp, argv[0])` と `child_exec(path, argv, shell->envp)` → 両方 `shell->env`。
- `src/path_utils.c` の `find_command_path(char **envp,...)` / `find_path_env` は **引数走査のみでコード無変更**。呼び出し元が `shell->env` を渡せば export PATH= が反映される(プロトタイプの env_set 上書きで PATH を差し替えれば後続 access が新パスを見るのは自明)。

## 4. 42 Norm 注意(本Issueで増える論点)
- 新規5ファイル。各 ≤5関数。env ヘルパは env_utils.c(4) / env_set.c(4) に分割の設計が妥当。
- env_set / env_dup は malloc・ループ・条件分岐で **25行を超えやすい**。`env_count` を別関数に出す、key 一致判定を `env_match` ヘルパに出す等で分割(設計どおり)。
- 三項禁止 → malloc 失敗分岐は if/else。`while` のみ。
- export の `=` 分割・`is_valid_id` は素直だが、`is_valid_id` のループ + 先頭判定を25行に収める。

## 5. リスク箇所(上位3)
### リスク1: env_set 新規追加の旧外枠 free 忘れ / 要素二重 free
- 回避: 「旧要素ポインタを新配列へ移譲 → **旧外枠だけ** free(`free(env)`、要素は free しない)」。プロトタイプで leaks 0 を確認済み。Reviewer は free 対象が外枠のみかをコードで確認。

### リスク2: cd の PWD がシンボリックリンクで bash と乖離(上記2)
- 回避: 最小は getcwd ベースで割り切り、既知差分として申し送り。`cd ..`/通常ディレクトリでは一致。

### リスク3: export 一時 key の free 漏れ / 不正識別子 early return 経路
- 回避: `=` 分割で `ft_strndup` した key は env_set 後に必ず free。不正 id で early return する経路でも free を通す(goto 不可なら構造で保証)。leaks で export 連打して確認。

## 6. Implementer が叩く検証コマンド
```bash
make
printf 'cd /tmp\npwd\nenv\nexit\n' | ./minishell        # PWD 反映(/private/tmp に注意)
printf 'cd ..\npwd\nexit\n' | ./minishell
printf 'export A=1\nenv\nexport A=2\nenv\nunset A\nenv\nexit\n' | ./minishell | grep -c '^A='  # 1 -> 1 -> 0
printf 'export ?bad=1\nexit\n' | ./minishell            # not a valid identifier, status 1
printf 'cd /nonexistent\npwd\nexit\n' | ./minishell      # cd エラー + カレント不変
printf 'ls\npwd\necho -n hi\nenv\n' | ./minishell        # 既存回帰
# リーク(builtins 主体で。ls 等 fork 経路は leaks 計装が重い)
printf 'export A=1\nexport A=2\nunset A\ncd /tmp\ncd ..\nenv\n' | leaks --atExit -- ./minishell 2>&1 | grep 'leaks for'
/tmp/pdfvenv/bin/norminette src include libft   # ヘッダー以外クリーンか
```

## 申し送り
1. **42 ヘッダー未付与**(ログイン待ち)継続。norminette は `INVALID_HEADER` のみが想定。
2. **PWD シンボリックリンク差分**(getcwd 物理パス)は最小実装の既知差分として PR/Reviewer に明記。
3. **Implementer が固定する2判断**(設計どおり): 引数なし `cd` = no-op で return / 値なし `export NAME` = 何もしない。DoD に明記。
4. `leaks` は `ls` 等 fork 経路を含めると計装が重く遅延/ハングしやすい(#4 で観測)。リーク確認は builtins/env 操作主体の入力で行う。

## 関連ファイル
- 設計入力: `docs/pipeline/issue-6/01-design.md`
- 本成果物: `docs/pipeline/issue-6/02-research.md`
