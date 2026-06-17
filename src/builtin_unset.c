#include "minishell.h"

/*
** unset NAME [NAME...]。各引数を env から削除(該当なしは no-op で status 0)。
** オプション(-v 等)や識別子検証は最小仕様のためスコープ外。
*/
int	bi_unset(t_shell *shell, char **argv)
{
	int	i;

	i = 1;
	while (argv[i])
	{
		shell->env = env_unset(shell->env, argv[i]);
		i++;
	}
	shell->last_status = 0;
	return (0);
}
