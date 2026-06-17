# 03-implementation.md

- **Issue:** #2 プロジェクト基盤(最小 REPL スケルトン)
- **Stage:** 3/5 Implementer
- **Branch:** `feat/issue-2-foundation`
- **実行者:** パイプライン本体(サブエージェント側で Bash 権限が拒否されたため、メインが Implementer 役を直接実行)

## 作成ファイル一覧
| ファイル | 役割 |
|---|---|
| `Makefile` | 本体ビルド。`-Wall -Wextra -Werror`、`cc`、ルール `all/clean/fclean/re/$(NAME)`、readline 動的パス解決、libft sub-make 先行ビルド |
| `include/minishell.h` | `t_shell`(`char **envp; int last_status;`)定義 + プロトタイプ |
| `src/main.c` | `main` / `shell_init`(envp 格納) / `shell_cleanup`(rl_clear_history) |
| `src/repl.c` | `repl_loop`(while/ctrl-D で改行+break/空行は add_history せず) + `is_blank_line` ヘルパ |
| `src/process_line.c` | `process_line`(将来レキサ接続点。現状は ft_strlen 経由でエコー) |
| `libft/libft.h`, `libft/ft_strlen.c`, `libft/Makefile` | 最小 libft(`ft_strlen` のみ。libft.a 生成・再リンク回避) |
| `.gitignore` | `*.o` `*.a` `minishell` `.DS_Store` を除外 |

## 設計からの逸脱
- **なし(機能面)**。01-design の方針(グローバル0個 / t_shell ポインタ渡し / process_line 1接続点 / readline 動的解決 / libft 最小同梱)をそのまま実装。
- 軽微な実装判断: `process_line` のエコーを `printf` ではなく `write + ft_strlen` で実装。理由 = libft のリンクを実際に成立させ(器が本当に繋がっていることを実証)、許可関数 `write` のみを使うため。

## 実行した検証コマンドと実際の出力
すべてメインの Bash で実測(2026-06-17, Intel Mac / Apple clang 14 / GNU Make 3.81)。

### 1. ビルド(警告ゼロ)
`make` → libft.a 生成 → src/*.o → minishell リンク。**警告/エラー無しで成功**。
```
cc -Wall -Wextra -Werror -Iinclude -Ilibft -I/usr/local/opt/readline/include -c src/main.c -o src/main.o
... (repl.c, process_line.c) ...
cc ... src/main.o src/repl.o src/process_line.o libft/libft.a -L/usr/local/opt/readline/lib -lreadline -o minishell
```

### 2. 不要な再リンク回避(リスク3)
- `make`(二度目) → `make: Nothing to be done for 'all'.` ✅ 再ビルドなし
- `touch include/minishell.h && make` → src/*.o が再コンパイルされ minishell が再リンク ✅ ヘッダ依存が効いている

### 3. readline リンク先(リスク1)
`otool -L ./minishell | grep -iE 'readline|edit'` →
```
/usr/local/opt/readline/lib/libreadline.8.dylib (compatibility version 8.3.0, current version 8.3.0)
```
✅ Homebrew 本物の libreadline を掴む(libedit ではない)。

### 4. clean 系
`make fclean && make` → fclean+make OK、binary 再生成を確認 ✅

### 5. 非対話スモークテスト
- `printf 'echo hi\n' | ./minishell` →
  ```
  minishell$ echo hi
  echo hi
  minishell$ exit
  ```
  ✅ 入力行をエコーし、EOF で `exit` 出力して終了
- `printf '' | ./minishell`(空 stdin = 即 EOF) → `minishell$ exit` ✅ 正常終了
- `printf '   \n\t\n\n' | ./minishell` → 空白/空行は**エコーされず**(process_line に渡らず)プロンプトのみ、最後に `exit` ✅ クラッシュ無し

### 6. 自作リーク(readline 由来は除外)
`printf 'echo hi\n' | leaks --atExit -- ./minishell` →
```
Process NNNNN: 0 leaks for 0 total leaked bytes.
```
✅ 0 リーク。`free(line)` が全反復で実行されている。

### 7. norminette
- システム導入は PEP 668 で不可。venv(`/tmp/pdfvenv`)に導入して実行。
- 結果: **全ファイルで `INVALID_HEADER`(42 ヘッダー未付与)のみ**。それ以外の Norm 違反(関数25行 / 5関数/ファイル / while のみ / グローバル0個 / 80列)は**検出ゼロ**。

## Reviewer への申し送り / 未解決事項
1. **【既知・受容】42 ヘッダー未付与** — norminette が全ファイルで `INVALID_HEADER` を報告。ユーザー判断により**本段階ではヘッダーを付与しない**(42 ログイン確定待ち)。ログイン確定後に全ファイル一括付与で解消可能。**これは Reviewer の must 差し戻し対象ではなく、ログイン待ちの保留事項として扱う。**
2. ヘッダー以外の Norm はクリーン。機能 DoD はすべて実測で充足(下記)。
3. 対話シグナル(ctrl-C/ctrl-\)はスコープ外(別Issue)。非対話スモークのみ実施。

## DoD 充足状況
- [x] make 警告/エラー無し
- [x] 二度目 make で再ビルドなし / ヘッダ変更で再ビルド
- [x] make clean/fclean/re/all 機能、libft 連動
- [x] プロンプト表示・history(add_history)・ctrl-D で `exit`+終了
- [x] 空行/空白のみでクラッシュ無し(履歴にも積まない)
- [x] process_line が入力行を受け取れる(将来差し替え点)
- [x] 自作コード 0 リーク
- [x] Norm: 25行/5関数/グローバル0個/while のみ(**ヘッダーのみ保留**)
- [x] macOS(Homebrew readline)でビルド可

## ブランチ / コミット
- ブランチ: `feat/issue-2-foundation`
- コミットハッシュ: (コミット後に追記)
