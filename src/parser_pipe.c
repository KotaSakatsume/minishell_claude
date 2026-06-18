#include <stdlib.h>
#include "minishell.h"

/*
** `|` を処理。直前 cmd が空(argv も redirs も無い=先頭 `|` や `||`)、
** または `|` が末尾(次トークン無し)なら構文エラー。
** それ以外は新 cmd を cur->next に連結し cur と toks を進める。
*/
int	p_pipe(t_shell *shell, t_cmd **pcur, t_tok **ptoks)
{
	t_cmd	*next;

	if (!(*pcur)->argv[0] && !(*pcur)->redirs)
		return (parse_error(shell));
	if (!(*ptoks)->next)
		return (parse_error(shell));
	next = cmd_new();
	if (!next)
		return (-1);
	(*pcur)->next = next;
	*pcur = next;
	*ptoks = (*ptoks)->next;
	return (0);
}
