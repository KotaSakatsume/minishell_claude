#include <stdlib.h>
#include <unistd.h>
#include "minishell.h"

/*
** exit ビルトイン(引数なし最小仕様)。親プロセスを終了させる。
** process_line の free_argv には戻らないため、ここで argv を解放してから exit。
*/
int	bi_exit(t_shell *shell, char **argv)
{
	int	status;

	status = shell->last_status;
	write(STDOUT_FILENO, "exit\n", 5);
	free_argv(argv);
	exit(status);
}
