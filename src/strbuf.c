#include <stdlib.h>
#include "minishell.h"

int	sb_init(t_buf *b)
{
	b->data = (char *)malloc(16);
	if (!b->data)
		return (1);
	b->len = 0;
	b->cap = 16;
	return (0);
}

/* realloc 非許可のため、新 cap=cap*2 を malloc し手動コピーして差し替える */
static int	sb_grow(t_buf *b)
{
	char	*nd;
	size_t	i;

	nd = (char *)malloc(b->cap * 2);
	if (!nd)
		return (1);
	i = 0;
	while (i < b->len)
	{
		nd[i] = b->data[i];
		i++;
	}
	free(b->data);
	b->data = nd;
	b->cap = b->cap * 2;
	return (0);
}

int	sb_push(t_buf *b, char c)
{
	if (b->len == b->cap && sb_grow(b))
		return (1);
	b->data[b->len++] = c;
	return (0);
}

int	sb_push_str(t_buf *b, const char *s)
{
	size_t	i;

	i = 0;
	while (s[i])
	{
		if (sb_push(b, s[i]))
			return (1);
		i++;
	}
	return (0);
}

/* 末尾に '\0' を足して完成した C 文字列を返し、所有を手放す(以降 b は空) */
char	*sb_release(t_buf *b)
{
	char	*ret;

	if (sb_push(b, '\0'))
		return (NULL);
	ret = b->data;
	b->data = NULL;
	b->len = 0;
	b->cap = 0;
	return (ret);
}
