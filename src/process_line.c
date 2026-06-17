#include "minishell.h"

/*
** 将来のレキサ/パーサ/実行の接続点(拡張ポイント)。
** 後段Issueはこの関数の中身を tokenize -> parse -> execute に差し替えるだけで済む。
** line の所有権は呼び出し側(repl_loop)が持ち、本関数は読むだけ。
** スケルトンの現状は入力行をそのままエコーするに留める(libft の ft_strlen を経由)。
*/
int	process_line(t_shell *shell, char *line)
{
	(void)shell;
	write(STDOUT_FILENO, line, ft_strlen(line));
	write(STDOUT_FILENO, "\n", 1);
	return (0);
}
