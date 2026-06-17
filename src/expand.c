#include <stdlib.h>
#include "minishell.h"

/* $VAR の変数名長(先頭=英字/_、以降=英数字/_)。名前にならなければ 0 */
int	var_name_len(const char *s)
{
	int	n;

	if (!((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z')
			|| s[0] == '_'))
		return (0);
	n = 0;
	while ((s[n] >= 'a' && s[n] <= 'z') || (s[n] >= 'A' && s[n] <= 'Z')
		|| s[n] == '_' || (s[n] >= '0' && s[n] <= '9'))
		n++;
	return (n);
}

/* $? を 10進文字列化して push。last_status は 0..255 想定、負値も防御的に対応 */
int	append_status(t_buf *b, int n)
{
	char	tmp[12];
	int		i;

	if (n == 0)
		return (sb_push(b, '0'));
	i = 0;
	if (n < 0)
	{
		if (sb_push(b, '-'))
			return (1);
		n = -n;
	}
	while (n > 0)
	{
		tmp[i++] = '0' + (n % 10);
		n = n / 10;
	}
	while (i > 0)
		if (sb_push(b, tmp[--i]))
			return (1);
	return (0);
}

/* $NAME: env_get で値取得し push。未定義は空。名前にならなければリテラル $ */
static int	expand_named(t_shell *shell, t_lex *lx, const char *s, int i)
{
	int		len;
	char	*name;
	char	*val;

	len = var_name_len(s + i + 1);
	if (len == 0)
	{
		if (sb_push(&lx->buf, '$'))
			return (-1);
		return (i + 1);
	}
	name = ft_strndup(s + i + 1, len);
	if (!name)
		return (-1);
	val = env_get(shell->env, name);
	free(name);
	if (val && sb_push_str(&lx->buf, val))
		return (-1);
	return (i + 1 + len);
}

/* s[i]=='$' 前提。展開して push し、次の走査 index を返す(-1=確保失敗) */
int	expand_var(t_shell *shell, t_lex *lx, const char *s, int i)
{
	if (s[i + 1] == '?')
	{
		if (append_status(&lx->buf, shell->last_status))
			return (-1);
		return (i + 2);
	}
	return (expand_named(shell, lx, s, i));
}
