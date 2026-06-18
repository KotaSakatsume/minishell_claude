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

/* トークン型(WORD = 語、他はリダイレクト演算子) */
typedef enum e_tok_type
{
	TOK_WORD,
	TOK_IN,
	TOK_OUT,
	TOK_APPEND,
	TOK_HEREDOC,
	TOK_PIPE
}	t_tok_type;

/* t_tok: レキサ出力の型付きトークン連結リスト */
typedef struct s_tok
{
	t_tok_type		type;
	char			*str;
	struct s_tok	*next;
}	t_tok;

/* t_redir: パーサ出力の出現順リダイレクトリスト */
typedef struct s_redir
{
	t_tok_type		type;
	char			*target;
	int				fd;
	struct s_redir	*next;
}	t_redir;

/* t_cmd: コマンド(argv + リダイレクト)。next で `|` パイプライン連結 */
typedef struct s_cmd
{
	char			**argv;
	t_redir			*redirs;
	struct s_cmd	*next;
}	t_cmd;

/* t_lex: レキサ状態(トークン列構築 + quote 状態 + 語 active フラグ) */
typedef struct s_lex
{
	t_tok	*head;
	t_tok	*tail;
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
t_tok	*lex_tokens(t_shell *shell, const char *line);
int		lex_run(t_shell *shell, t_lex *lx, const char *line);
int		finish_word(t_lex *lx);
int		tok_push(t_lex *lx, t_tok_type type, char *str);

/* lexer_state.c */
int		handle_char(t_shell *shell, t_lex *lx, const char *s, int i);

/* lexer_op.c */
int		is_op(char c);
int		op_len(const char *s, int i);
int		op_type(const char *s, int i, int len);
int		emit_op(t_lex *lx, const char *s, int i);

/* parser.c */
t_cmd	*parse_tokens(t_shell *shell, t_tok *toks);
int		parse_error(t_shell *shell);

/* parser_pipe.c */
int		p_pipe(t_shell *shell, t_cmd **pcur, t_tok **ptoks);

/* pipeline.c */
int		exec_pipeline(t_shell *shell, t_cmd *head);

/* pipeline_child.c */
void	pipe_child(t_shell *shell, t_cmd *cmd, int in_fd, int *pfd);

/* cmd.c */
t_cmd	*cmd_new(void);
int		argv_push_cmd(t_cmd *cmd, char *word);
void	free_cmd(t_cmd *cmd);
void	free_redirs(t_redir *r);
void	free_tok(t_tok *t);

/* redir.c */
int		apply_redirs(t_redir *r);
int		restore_fds(int *saved);

/* heredoc.c */
int		run_heredocs(t_shell *shell, t_cmd *cmd);

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
int		run_external(t_shell *shell, t_cmd *cmd);
void	exec_external(t_shell *shell, t_cmd *cmd, char *path);
int		wait_status(int status);

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
