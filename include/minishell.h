#ifndef MINISHELL_H
# define MINISHELL_H

# include <stdio.h>
# include <stdlib.h>
# include <unistd.h>
# include <readline/readline.h>
# include <readline/history.h>
# include "libft.h"

/*
** t_shell: シェル全体の状態。main で1つ確保し全関数へポインタ渡しする。
** グローバル変数は導入しない(将来のシグナル用に枠を温存)。
**   env         : 起動時 envp を複製して所有する可変コピー
**                 (env / PATH 参照 / execve / cd・export・unset が読み書き)
**   last_status : 直近コマンドの終了ステータス($? 用)
*/
typedef struct s_shell
{
	char	**env;
	int		last_status;
}	t_shell;

/* t_buf: 自前可変長文字列(realloc 非許可のため倍化 malloc で grow) */
typedef struct s_buf
{
	char	*data;
	size_t	len;
	size_t	cap;
}	t_buf;

/* t_lex: レキサ状態(argv 構築 + quote 状態 + 語 active フラグ) */
typedef struct s_lex
{
	char	**argv;
	int		argc;
	t_buf	buf;
	int		quote;
	int		active;
	int		had_quote;
}	t_lex;

/* main.c */
int		shell_init(t_shell *shell, char **envp);
void	shell_cleanup(t_shell *shell);

/* repl.c */
int		repl_loop(t_shell *shell);

/* process_line.c (将来のレキサ/実行の接続点) */
int		process_line(t_shell *shell, char *line);

/* tokenize.c */
void	free_argv(char **argv);

/* lexer.c */
char	**tokenize(t_shell *shell, const char *line);
int		lex_run(t_shell *shell, t_lex *lx, const char *line);
int		finish_word(t_lex *lx);
int		argv_push(t_lex *lx, char *word);

/* lexer_state.c */
int		handle_char(t_shell *shell, t_lex *lx, const char *s, int i);

/* expand.c */
int		expand_var(t_shell *shell, t_lex *lx, const char *s, int i);
int		var_name_len(const char *s);
int		append_status(t_buf *b, int n);

/* strbuf.c */
int		sb_init(t_buf *b);
int		sb_push(t_buf *b, char c);
int		sb_push_str(t_buf *b, const char *s);
char	*sb_release(t_buf *b);

/* builtins.c */
int		is_builtin(const char *cmd);
int		run_builtin(t_shell *shell, char **argv);

/* builtin_exit.c */
int		bi_exit(t_shell *shell, char **argv);

/* builtin_cd.c */
int		bi_cd(t_shell *shell, char **argv);

/* builtin_export.c */
int		bi_export(t_shell *shell, char **argv);

/* builtin_unset.c */
int		bi_unset(t_shell *shell, char **argv);

/* execute.c */
int		run_external(t_shell *shell, char **argv);

/* path_utils.c */
char	*find_command_path(char **env, const char *cmd);

/* env_utils.c */
int		env_count(char **env);
char	**env_dup(char **src);
char	*env_get(char **env, const char *key);
void	env_free(char **env);
char	**env_unset(char **env, const char *key);

/* env_set.c */
char	**env_set(char **env, const char *key, const char *val);

#endif
