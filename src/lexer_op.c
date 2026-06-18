#include "minishell.h"

/* クォート外のリダイレクト演算子起点文字か */
int	is_op(char c)
{
	return (c == '<' || c == '>');
}

/* `>>` / `<<` は2文字、`>` / `<` は1文字 */
int	op_len(const char *s, int i)
{
	if (s[i + 1] == s[i])
		return (2);
	return (1);
}

int	op_type(const char *s, int i, int len)
{
	if (s[i] == '<')
	{
		if (len == 2)
			return (TOK_HEREDOC);
		return (TOK_IN);
	}
	if (len == 2)
		return (TOK_APPEND);
	return (TOK_OUT);
}

/* 進行中の語を確定して語境界を作り、演算子トークンを emit。次 index を返す */
int	emit_op(t_lex *lx, const char *s, int i)
{
	int	len;

	if (finish_word(lx))
		return (-1);
	len = op_len(s, i);
	if (tok_push(lx, op_type(s, i, len), NULL))
		return (-1);
	return (i + len);
}
