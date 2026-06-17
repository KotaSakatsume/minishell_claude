#include <stdlib.h>
#include "minishell.h"

static int	is_space(char c)
{
	return (c == ' ' || c == '\t');
}

/* シングルクォート中: ' で閉じる以外は全てリテラル($も展開しない) */
static int	handle_single(t_lex *lx, const char *s, int i)
{
	if (s[i] == '\'')
		lx->quote = 0;
	else if (sb_push(&lx->buf, s[i]))
		return (-1);
	return (i + 1);
}

/* ダブルクォート中: " で閉じる、$ は展開、それ以外はリテラル */
static int	handle_double(t_shell *shell, t_lex *lx, const char *s, int i)
{
	if (s[i] == '"')
		lx->quote = 0;
	else if (s[i] == '$')
		return (expand_var(shell, lx, s, i));
	else if (sb_push(&lx->buf, s[i]))
		return (-1);
	return (i + 1);
}

/* クォート外: 空白で語確定、' " で各クォート開始、$ 展開、他はリテラル */
static int	handle_none(t_shell *shell, t_lex *lx, const char *s, int i)
{
	if (is_space(s[i]))
	{
		if (finish_word(lx))
			return (-1);
		return (i + 1);
	}
	lx->active = 1;
	if (s[i] == '\'' || s[i] == '"')
	{
		lx->had_quote = 1;
		lx->quote = 1 + (s[i] == '"');
	}
	else if (s[i] == '$')
		return (expand_var(shell, lx, s, i));
	else if (sb_push(&lx->buf, s[i]))
		return (-1);
	return (i + 1);
}

int	handle_char(t_shell *shell, t_lex *lx, const char *s, int i)
{
	if (lx->quote == 1)
		return (handle_single(lx, s, i));
	if (lx->quote == 2)
		return (handle_double(shell, lx, s, i));
	return (handle_none(shell, lx, s, i));
}
