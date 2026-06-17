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
**   envp        : 環境変数(env / PATH 参照 / execve の第3引数)
**   last_status : 直近コマンドの終了ステータス($? 用)
*/
typedef struct s_shell
{
	char	**envp;
	int		last_status;
}	t_shell;

/* main.c */
int		shell_init(t_shell *shell, char **envp);
void	shell_cleanup(t_shell *shell);

/* repl.c */
int		repl_loop(t_shell *shell);

/* process_line.c (将来のレキサ/実行の接続点) */
int		process_line(t_shell *shell, char *line);

/* tokenize.c */
char	**tokenize(const char *line);
void	free_argv(char **argv);

/* builtins.c */
int		is_builtin(const char *cmd);
int		run_builtin(t_shell *shell, char **argv);

/* builtin_exit.c */
int		bi_exit(t_shell *shell, char **argv);

/* execute.c */
int		run_external(t_shell *shell, char **argv);

/* path_utils.c */
char	*find_command_path(char **envp, const char *cmd);

#endif
