#include <stdlib.h>
#include "minishell.h"

/*
** argv(NULL終端、各要素 malloc)の解放。lexer が構築した argv を
** process_line 末尾で解放する経路で使う(Issue #8 でレキサ化後も不変)。
*/
void	free_argv(char **argv)
{
	int	i;

	if (!argv)
		return ;
	i = 0;
	while (argv[i])
	{
		free(argv[i]);
		i++;
	}
	free(argv);
}
