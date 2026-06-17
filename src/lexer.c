#include <stdlib.h>
#include <unistd.h>
#include "minishell.h"

/* 完成トークンを argv に追加(自前 grow: count+2 で確保し付け替え) */
int	argv_push(t_lex *lx, char *word)
{
	char	**na;
	int		i;

	na = (char **)malloc(sizeof(char *) * (lx->argc + 2));
	if (!na)
	{
		free(word);
		return (-1);
	}
	i = 0;
	while (i < lx->argc)
	{
		na[i] = lx->argv[i];
		i++;
	}
	na[lx->argc] = word;
	na[lx->argc + 1] = NULL;
	free(lx->argv);
	lx->argv = na;
	lx->argc++;
	return (0);
}

/* active な語を確定(buf を release して argv へ移譲し、buf を作り直す) */
int	finish_word(t_lex *lx)
{
	char	*word;

	if (!lx->active)
		return (0);
	word = sb_release(&lx->buf);
	if (!word)
		return (-1);
	if (argv_push(lx, word))
		return (-1);
	lx->active = 0;
	if (sb_init(&lx->buf))
		return (-1);
	return (0);
}

/* 未閉じクォート: エラー表示して行を破棄、last_status=2 を立てる */
static int	lex_abort(t_shell *shell)
{
	write(STDERR_FILENO, "minishell: syntax error: unclosed quote\n", 40);
	shell->last_status = 2;
	return (-1);
}

int	lex_run(t_shell *shell, t_lex *lx, const char *line)
{
	int	i;

	i = 0;
	while (line[i])
	{
		i = handle_char(shell, lx, line, i);
		if (i < 0)
			return (-1);
	}
	if (lx->quote != 0)
		return (lex_abort(shell));
	return (finish_word(lx));
}

char	**tokenize(t_shell *shell, const char *line)
{
	t_lex	lx;

	lx.argv = (char **)malloc(sizeof(char *));
	if (!lx.argv)
		return (NULL);
	lx.argv[0] = NULL;
	lx.argc = 0;
	lx.quote = 0;
	lx.active = 0;
	if (sb_init(&lx.buf))
	{
		free(lx.argv);
		return (NULL);
	}
	if (lex_run(shell, &lx, line))
	{
		free(lx.buf.data);
		free_argv(lx.argv);
		return (NULL);
	}
	free(lx.buf.data);
	return (lx.argv);
}
