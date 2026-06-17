# 01-design.md

- **Issue:** #2 プロジェクト基盤(最小実行可能スケルトン)
- **Stage:** 1/5 Architect

## 方針 (1行)
readline ベースの REPL ループだけを持つ最小スケルトンを作り、入力行を「次段(レキサ)へ渡す境界関数」を1つ用意してそこで今はエコーするに留める。レキサ/パーサ/実行/シグナルは触らない。

## 設計方針 (5-7行)
- **アーキテクチャ:** `t_shell` 状態構造体を `main` で1つ確保し、全関数へポインタ渡し(グローバル変数は本Issueでは導入しない=ノルムの1個枠を温存)。
- **データフロー:** `main` → `shell_init(&shell, envp)` → `repl_loop(&shell)` → 各反復で `readline(prompt)` → 空判定 → `add_history` → `process_line(&shell, line)`(現状はエコー or no-op)→ `free(line)`。
- **REPL制御フロー:** `readline` が `NULL`(ctrl-D/EOF)を返したら改行を出して `break`。空行/空白のみは履歴に積まず即 continue(クラッシュ防止)。各反復末で必ず `free(line)`。
- **主要インターフェース:** `int shell_init(t_shell*, char **envp)` / `int repl_loop(t_shell*)` / `int process_line(t_shell*, char *line)`(=将来のレキサ呼び出し点)/ `void shell_cleanup(t_shell*)`。
- **DB変更:** なし(該当しない)。
- **エラーハンドリング:** `malloc`/`shell_init` 失敗時は `perror` 相当 + 非ゼロ終了。`readline` 由来メモリは免除、自作分は全 free。

## 採用理由とトレードオフ
- **採用:** `t_shell` ポインタ渡し + グローバル不使用。理由: シグナル本実装(別Issue)までグローバル1個枠を空けておけ、テスト時に状態を注入しやすい。
- **却下A:** 今からグローバル `t_shell` を置く案。理由: ノルムのグローバル1個制約をシグナル用に温存できなくなり、後段で設計やり直しになる。
- **却下B:** libft を本Issueでフル移植する案。理由: スケルトンに不要な実装が膨らみ1PRを超える。最小スタブのみ同梱(下記)。

## 技術選定
### ディレクトリ構成
```
minishell/
├── Makefile
├── include/
│   └── minishell.h        # t_shell 定義 + プロトタイプ
├── src/
│   ├── main.c             # main, shell_init, shell_cleanup
│   ├── repl.c             # repl_loop, 空行判定ヘルパ
│   └── process_line.c     # process_line(将来レキサの接続点)
└── libft/
    ├── Makefile           # libft.a を生成
    ├── libft.h
    └── ft_*.c             # 今回使う最小分のみ(下記)
```
ファイルあたり5関数以内・関数25行以内(42 Norm)。main.c が膨らむ場合は init を別ファイルへ分割可。

### readline リンク方法(macOS Homebrew前提)
- Homebrew の readline は keg-only。Makefile で動的にパス解決する:
  - `READLINE_DIR := $(shell brew --prefix readline 2>/dev/null)`
  - 取得できた場合のみ `CFLAGS += -I$(READLINE_DIR)/include` / `LDFLAGS += -L$(READLINE_DIR)/lib`
  - リンクは `-lreadline`(必要なら `-lhistory`)。
- Linux(42 校内環境)では `brew` が無く `READLINE_DIR` が空になり、標準パスの `-lreadline` で解決 → macOS/Linux 両対応。ハードコードした `/opt/homebrew` や `/usr/local` は書かない(Apple Silicon/Intel 差を吸収するため brew --prefix に委ねる)。

### libft の扱い
- **最小同梱**。本体 Makefile が `libft/` を先にビルド(`$(NAME)` 依存に `$(LIBFT)` を入れ、`libft.a` ルールで `$(MAKE) -C libft`)。
- 今回必要な関数のみ移植: 入力トリム/空白判定に使う `ft_strlen` 程度。空白のみ判定は許可関数(`isatty` 等)に空白判定は無いので libft 側のヘルパで実装。
- libft を空にはしない(後段Issueで必ず使うため器を先に通しておく)。ただし無関係な関数は入れない。

## スコープ(影響範囲)
- **作る:** `Makefile`、`include/minishell.h`、`src/main.c`、`src/repl.c`、`src/process_line.c`、`libft/`(最小)。
- **変更行数オーダー:** 全体で約 150-250 行(うち Makefile 40-60 行、libft 最小数十行)。1PRで完結するサイズ。
- **モジュール:** REPL ループとシェル状態の初期化/破棄、ビルド基盤のみ。

## やらないこと(3点)
1. レキサ/パーサ/トークン化・実行(execve/fork/pipe/リダイレクト)・組み込みコマンド — 別Issue。
2. シグナル処理(ctrl-C/ctrl-\、`sigaction`、グローバル変数導入) — 別Issue。`process_line` 内のロジックも今は書かない。
3. 環境変数の複製・展開、`$?`、引用符処理、ヒアドキュメント — 別Issue。`envp` は `shell_init` で受けるが今は保持/エコー程度に留める。

## 後続Issueへの拡張ポイント
- **接続点は `process_line(t_shell*, char *line)` 1関数**。レキサIssueはこの中身を `tokenize → parse → execute` に差し替えるだけで済む。シグネチャは将来も変えない(line は所有権を呼び出し側=repl が持ち、process_line は読むだけ)。
- **`t_shell` 構造体**を拡張ポイントとして用意: 今は `char **envp`(または将来の複製先 `char **env`)と `int last_status`(`$?` 用、初期化のみ)フィールドを置いておく。後段が構造体にフィールドを足すだけで状態を引き回せる。
- **Makefile の SRCS 変数**にファイルを追記するだけでビルド対象が増える形にする(ワイルドカード非依存、明示列挙で不要再リンク回避)。

## 完了条件(Definition of Done)
- [ ] `make` がエラー/警告なく成功(`-Wall -Wextra -Werror`)。`make` 二度目は再ビルドが走らない(不要な再リンク禁止)。
- [ ] `make clean` / `make fclean` / `make re` / `make all` が全て機能し、libft も連動。
- [ ] 起動すると `minishell$ ` 等のプロンプトが表示される(REPL)。
- [ ] 文字入力 → Enter で `add_history` に積まれ、上矢印で履歴が呼べる。
- [ ] ctrl-D(EOF)でプロンプトが終了し、シェルが正常終了する(改行付き)。
- [ ] 空行のみ・空白のみ入力でクラッシュしない(セグフォ/異常終了なし、履歴にも積まない)。
- [ ] `process_line` が存在し、入力行を受け取れる(現状エコー or no-op で可)。後段が差し込める。
- [ ] 自作コードに valgrind/leaks でリーク無し(readline 由来は除外)。`free(line)` が全反復で実行される。
- [ ] 42 Norm: 関数25行以内・1ファイル5関数以内・グローバル変数0個。
- [ ] macOS(Homebrew readline)と Linux 標準パスの両方でビルド可能(`brew --prefix` 動的解決)。
