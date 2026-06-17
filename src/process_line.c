#include "minishell.h"

/*
** builtin / external の分岐。builtin は親プロセスで実行する
** (exit が親を終了させ、将来の cd を親に効かせるため fork しない)。
*/
static int	dispatch(t_shell *shell, char **argv)
{
	if (is_builtin(argv[0]))
		return (run_builtin(shell, argv));
	return (run_external(shell, argv));
}

/*
** 将来のレキサ/パーサ/実行の接続点。
** 現状は「空白split -> dispatch -> argv 解放」。クォート/展開/リダイレクト/
** パイプは別Issue。argv の所有権は本関数が持ち、末尾で必ず解放する。
*/
int	process_line(t_shell *shell, char *line)
{
	char	**argv;

	argv = tokenize(shell, line);
	if (!argv || !argv[0])
	{
		free_argv(argv);
		return (0);
	}
	dispatch(shell, argv);
	free_argv(argv);
	return (0);
}
