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
** グローバル変数は本Issueでは導入しない(将来のシグナル用に枠を温存)。
**   envp        : 起動時の環境変数(今は保持のみ。後段で複製/展開に使う)
**   last_status : 直近の終了ステータス($? 用。今は初期化のみ)
*/
typedef struct s_shell
{
	char	**envp;
	int		last_status;
}	t_shell;

int		shell_init(t_shell *shell, char **envp);
void	shell_cleanup(t_shell *shell);
int		repl_loop(t_shell *shell);
int		process_line(t_shell *shell, char *line);

#endif
