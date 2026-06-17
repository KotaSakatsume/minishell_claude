# 02-research.md

- **Issue:** #2 プロジェクト基盤(最小 REPL スケルトン)
- **Stage:** 2/5 Investigator
- **実測環境:** macOS (Darwin 21.6.0 / Intel), 2026-06-17 にパイプライン側 Bash で実測

> 注: サブエージェント側は Bash 権限が無く実測できなかったため、パイプライン本体(メイン)が実測コマンドを代行し、本パックの「実測」欄を実値で埋めた。

---

## 1. 開発環境 — 【実測済み】

| 確認項目 | 実測結果 |
|---|---|
| `brew --prefix readline` | **`/usr/local/opt/readline`**(Intel Mac。Apple Silicon なら `/opt/homebrew/opt/readline`) |
| `include/readline/` | `chardefs.h history.h keymaps.h readline.h rlconf.h rlstdc.h rltypedefs.h tilde.h` — **実在** |
| `lib/` readline | `libreadline.8.3.dylib libreadline.8.dylib libreadline.a libreadline.dylib` — **実在(.a/.dylib 両方)** |
| `cc --version` | **Apple clang version 14.0.0** |
| `make --version` | **GNU Make 3.81** — GNU make。`:=` / `$(shell ...)` 構文が使える(BSD make ではない) |
| readline リンク最小プログラム | `cc -Wall -Wextra -Werror -I.../include probe.c -L.../lib -lreadline` → **LINK OK** |
| `otool -L probe \| grep readline` | **`/usr/local/opt/readline/lib/libreadline.8.dylib`** を掴む — **libedit ではない(リスク1はこの環境で解消)** |
| `norminette` | **未インストール**(`which norminette` → 該当なし)。下記「申し送り」参照 |

**確定事実:**
- `brew --prefix readline` は `/usr/local/opt/readline` を返す。`$(shell brew --prefix readline)` での動的解決(01-design.md:42-45)は**この環境で正しく機能する**。ハードコード不要。
- Homebrew readline は keg-only のため `-I`/`-L` 指定が macOS で必須。実測で `-I.../include -L.../lib` を付けてリンク成功を確認。
- `-L.../lib` を付けた状態では `-lreadline` が **Homebrew の本物 libreadline.8.dylib** を掴む(otool で確認)。libedit 取り違え(リスク1)はこのマシンでは発生していない。

---

## 2. Makefile の落とし穴(定石)

### 2-1. readline パス解決(設計の核心)
```make
READLINE_DIR := $(shell brew --prefix readline 2>/dev/null)
ifneq ($(READLINE_DIR),)
  CFLAGS  += -I$(READLINE_DIR)/include
  LDFLAGS += -L$(READLINE_DIR)/lib
endif
LDLIBS += -lreadline
```
- **落とし穴A:** `brew` 不在の Linux(42校内環境)では `$(shell ...)` が空文字を返し `ifneq` が偽 → `-I/-L` 無しで標準パスの `-lreadline` にフォールバック。意図通り。`2>/dev/null` で brew 不在エラーを握りつぶすこと。
- **落とし穴B:** `-lreadline` は**リンク段**(`LDLIBS`/リンクコマンド末尾)に置く。`-c` コンパイル段に付けても無意味。

### 2-2. 不要な再リンク回避(DoD: make 二度目で再ビルドしない)
- `.o` を中間生成し `$(NAME): $(OBJS)` でオブジェクト依存にする。`.c` から直接 `$(NAME)` を作らない。
- ヘッダ依存を明示: `$(OBJS): include/minishell.h`。無いとヘッダ変更が反映されず、過剰だと毎回再ビルド。
- **libft 連携:** 実ファイルをターゲットに、sub-make へ再ビルド判断を委譲:
  ```make
  LIBFT = libft/libft.a
  $(LIBFT):
  	$(MAKE) -C libft
  ```
  libft Makefile 側も同様に再リンク抑止していれば、二度目の `make` は libft も本体も再ビルドしない。
- `SRCS` は明示列挙(ワイルドカード非依存。01-design.md:65)。`OBJS = $(SRCS:.c=.o)` で `src/` 構造を保つ。

### 2-3. `-Wall -Wextra -Werror`
- `-Werror` 下では**未使用引数が致命的**。`shell_init` の `envp`、`process_line` の `t_shell*` が今は no-op なので注意(リスク2)。

---

## 3. 42 Norm 該当ルール(本Issueで踏みやすい)

- **関数本体25行以内**。REPL ループ+空行判定を1関数に詰めると超える → 空行判定をヘルパ分離(01-design.md:32 妥当)。
- **1ファイル5関数以内**。`main.c` は main/shell_init/shell_cleanup の3つまでに抑える。
- **グローバル変数**: 本Issueは0個(将来シグナル用に1個枠を温存)。正しい判断。
- **1行80文字以内**(.c/.h)。Makefile は Norm 対象外。
- **変数宣言は関数先頭**。`while` 内宣言不可。
- **禁止構文:** `for` / `do-while` / 三項演算子 / `switch` / VLA。REPL は **`while`** で書く。
- **`norminette` を実行**: ただし**この環境では未インストール**。Implementer は `pip install norminette` でローカル導入するか、未導入のままなら Norm ルールを手目視で厳守し、その旨を 03 に明記すること。

---

## 4. readline 利用の罠

- **EOF(ctrl-D):** `readline()` は EOF で `NULL` 返却。NULL 時に改行出力して `break`(01-design.md:12)が正解。
- **空行/空白のみ:** `add_history` しない。ただし戻り値が `""`(非NULL)なら **free は必要**。NULL の時だけ free 不要。
- **`add_history` は内部で line をコピー**するので、呼び出し側は `add_history` 後も `free(line)` してよい(二重管理にならない)。設計と整合。
- **macOS の readline リーク:** `leaks`/valgrind が到達可能な内部バッファを報告するが readline 由来であり自作リークではない(DoD で除外)。
- **`rl_clear_history()`:** GNU readline 固有。本環境は Homebrew 本物 readline をリンクするので使えるが、本Issueでは必須ではない(readline 由来リークは免除)。入れるなら `shell_cleanup` で呼ぶ。
- **libedit 取り違え(macOS の最大の罠):** 一般には `-L` 指定が効かないと `-lreadline` が `/usr/lib/libedit` に解決され履歴が壊れる。**本環境では `-L.../lib` 指定下で Homebrew 本物を掴むことを otool で実測確認済み**(リスク1解消)。Implementer はビルド後に同じ otool 確認を行うこと。
- **プロンプト:** `readline("minishell$ ")`。色エスケープは使わない(素テキスト)。

---

## 5. リスク箇所(上位3つ・回避策付き)

### リスク1: macOS で `-lreadline` が libedit を掴み履歴が壊れる
- **本環境では実測で解消済み**(`-L/usr/local/opt/readline/lib` 下で otool が Homebrew libreadline.8.dylib を確認)。
- **回避策の徹底:** Makefile で `-L$(READLINE_DIR)/lib` を確実に付ける。Implementer はビルド後 `otool -L ./minishell | grep -E 'readline|edit'` で本物を掴んでいるか確認。

### リスク2: `-Werror` × 未使用 `envp` / 未使用 `t_shell` でビルドが落ちる
- **回避策:** `envp` は `shell->envp = envp;` で `t_shell` に格納(拡張ポイントと整合)。`process_line` で `t_shell*` を今使わないなら `last_status` を読む等の最小利用、または `(void)shell;`。

### リスク3: libft sub-make 連携で「二度目の make が再ビルドする」/ ヘッダ依存欠落
- **自動テストが無い**ため Implementer が手で確認しないと見逃す。
- **回避策:** (a) `$(LIBFT)` は実ファイル `libft/libft.a` をターゲットに sub-make 委譲。(b) `$(OBJS): include/minishell.h` を明示。(c) 手動実測: `make` → 即 `make`(再ビルド無し)→ `touch include/minishell.h && make`(再ビルドされる)の3点。

---

## 6. 既存テストの状況
- グリーンフィールド。テスト・CI・カバレッジは皆無。
- DoD のうち以下は**手動実測でしか担保できない** → Implementer の責務:
  - 再リンク無し(リスク3)
  - ctrl-D 終了 / 空行クラッシュ無し(対話的に手で叩く)
  - リーク無し(macOS: `leaks --atExit -- ./minishell`)
  - 履歴・プロンプト(対話的確認)

---

## 7. Implementer が着手前/完了時に叩く実測コマンド
```bash
# 着手前(本パックで実測済みだが、Apple Silicon 等別環境なら再確認)
brew --prefix readline      # => /usr/local/opt/readline (本環境)
cc --version ; make --version

# 完了時の検証
otool -L ./minishell | grep -E 'readline|edit'   # Homebrew 本物を掴むこと
make && make                                       # 二度目は再ビルド無し
touch include/minishell.h && make                  # ヘッダ変更で再ビルドされる
leaks --atExit -- ./minishell                      # 自作リーク無し(readline 由来は除外)
# norminette src include libft                      # ※未インストール環境では手目視
```

---

## 申し送り(要対応)
1. **norminette 未インストール** — この環境では Norm を機械チェックできない。Implementer は `pip install norminette` で導入するか、未導入のままなら Norm ルール(関数25行/5関数/while のみ/80列)を手目視で厳守し 03 にその旨を記載すること。
2. **Apple Silicon 差** — 本実測は Intel Mac(`/usr/local/opt/readline`)。`$(shell brew --prefix readline)` 動的解決なので Apple Silicon(`/opt/homebrew/...`)でも追従するが、別マシンでビルドする際は再確認。

## 関連ファイル
- 設計入力: `docs/pipeline/issue-2/01-design.md`
- 本成果物: `docs/pipeline/issue-2/02-research.md`
