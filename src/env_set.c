#include <stdlib.h>
#include "minishell.h"

static size_t	cpy(char *dst, const char *src)
{
	size_t	i;

	i = 0;
	while (src[i])
	{
		dst[i] = src[i];
		i++;
	}
	return (i);
}

static char	*make_kv(const char *key, const char *val)
{
	size_t	off;
	char	*kv;

	kv = (char *)malloc(ft_strlen(key) + ft_strlen(val) + 2);
	if (!kv)
		return (NULL);
	off = cpy(kv, key);
	kv[off++] = '=';
	off += cpy(kv + off, val);
	kv[off] = '\0';
	return (kv);
}

static int	env_find(char **env, const char *key)
{
	size_t	klen;
	int		i;

	klen = ft_strlen(key);
	i = 0;
	while (env && env[i])
	{
		if (!ft_strncmp(env[i], key, klen) && env[i][klen] == '=')
			return (i);
		i++;
	}
	return (-1);
}

static char	**env_append(char **env, char *kv, int n)
{
	char	**ne;
	int		i;

	ne = (char **)malloc(sizeof(char *) * (n + 2));
	if (!ne)
	{
		free(kv);
		return (env);
	}
	i = 0;
	while (i < n)
	{
		ne[i] = env[i];
		i++;
	}
	ne[n] = kv;
	ne[n + 1] = NULL;
	free(env);
	return (ne);
}

char	**env_set(char **env, const char *key, const char *val)
{
	char	*kv;
	int		idx;

	kv = make_kv(key, val);
	if (!kv)
		return (env);
	idx = env_find(env, key);
	if (idx >= 0)
	{
		free(env[idx]);
		env[idx] = kv;
		return (env);
	}
	return (env_append(env, kv, env_count(env)));
}
