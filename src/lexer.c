#include <stdlib.h>
#include <unistd.h>
#include "minishell.h"

/* 完成トークンを末尾に連結。str は所有を移譲(失敗時は free して -1) */
int	tok_push(t_lex *lx, t_tok_type type, char *str)
{
	t_tok	*tok;

	tok = (t_tok *)malloc(sizeof(t_tok));
	if (!tok)
	{
		free(str);
		return (-1);
	}
	tok->type = type;
	tok->str = str;
	tok->next = NULL;
	if (!lx->head)
		lx->head = tok;
	else
		lx->tail->next = tok;
	lx->tail = tok;
	return (0);
}

/* active な語を WORD トークンとして確定(unquoted の空語は捨てる) */
int	finish_word(t_lex *lx)
{
	char	*word;

	if (!lx->active)
		return (0);
	word = sb_release(&lx->buf);
	if (!word)
		return (-1);
	if (!lx->had_quote && word[0] == '\0')
		free(word);
	else if (tok_push(lx, TOK_WORD, word))
		return (-1);
	lx->active = 0;
	lx->had_quote = 0;
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

t_tok	*lex_tokens(t_shell *shell, const char *line)
{
	t_lex	lx;

	lx.head = NULL;
	lx.tail = NULL;
	lx.quote = 0;
	lx.active = 0;
	lx.had_quote = 0;
	if (sb_init(&lx.buf))
		return (NULL);
	if (lex_run(shell, &lx, line))
	{
		free(lx.buf.data);
		free_tok(lx.head);
		return (NULL);
	}
	free(lx.buf.data);
	return (lx.head);
}
