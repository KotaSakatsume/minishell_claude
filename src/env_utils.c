#include <stdlib.h>
#include "minishell.h"

int	env_count(char **env)
{
	int	i;

	i = 0;
	while (env && env[i])
		i++;
	return (i);
}

char	**env_dup(char **src)
{
	char	**env;
	int		n;
	int		i;

	n = env_count(src);
	env = (char **)malloc(sizeof(char *) * (n + 1));
	if (!env)
		return (NULL);
	i = 0;
	while (i < n)
	{
		env[i] = ft_strdup(src[i]);
		if (!env[i])
		{
			while (i > 0)
				free(env[--i]);
			free(env);
			return (NULL);
		}
		i++;
	}
	env[n] = NULL;
	return (env);
}

char	*env_get(char **env, const char *key)
{
	size_t	klen;
	int		i;

	klen = ft_strlen(key);
	i = 0;
	while (env && env[i])
	{
		if (!ft_strncmp(env[i], key, klen) && env[i][klen] == '=')
			return (env[i] + klen + 1);
		i++;
	}
	return (NULL);
}

void	env_free(char **env)
{
	int	i;

	if (!env)
		return ;
	i = 0;
	while (env[i])
	{
		free(env[i]);
		i++;
	}
	free(env);
}

char	**env_unset(char **env, const char *key)
{
	size_t	klen;
	int		i;
	int		j;

	klen = ft_strlen(key);
	i = 0;
	while (env && env[i])
	{
		if (!ft_strncmp(env[i], key, klen) && env[i][klen] == '=')
		{
			free(env[i]);
			j = i;
			while (env[j])
			{
				env[j] = env[j + 1];
				j++;
			}
		}
		else
			i++;
	}
	return (env);
}
