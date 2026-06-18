#include "minishell.h"

/*
** 空行 or 空白(スペース/タブ)のみの行かを判定する。
** これらは履歴に積まず、レキサにも渡さない(クラッシュ防止)。
*/
static int	is_blank_line(const char *line)
{
	int	i;

	i = 0;
	while (line[i])
	{
		if (line[i] != ' ' && line[i] != '\t')
			return (0);
		i++;
	}
	return (1);
}

/*
** REPL ループ。readline が NULL(ctrl-D/EOF)を返したら改行を出して終了。
** 非空行のみ add_history し process_line へ渡す。各反復で必ず free(line)。
*/
int	repl_loop(t_shell *shell)
{
	char	*line;

	while (1)
	{
		set_signals_interactive();
		line = readline("minishell$ ");
		if (g_signal == SIGINT)
		{
			shell->last_status = 130;
			g_signal = 0;
		}
		if (line == NULL)
		{
			write(STDOUT_FILENO, "exit\n", 5);
			break ;
		}
		if (!is_blank_line(line))
		{
			add_history(line);
			process_line(shell, line);
		}
		free(line);
	}
	return (shell->last_status);
}
