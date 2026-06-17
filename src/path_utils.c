#include <stdlib.h>
#include <unistd.h>
#include "minishell.h"

static char	*find_path_env(char **envp)
{
	int	i;

	i = 0;
	while (envp[i])
	{
		if (!ft_strncmp(envp[i], "PATH=", 5))
			return (envp[i] + 5);
		i++;
	}
	return (NULL);
}

static char	*path_join(const char *dir, size_t dlen, const char *cmd)
{
	size_t	clen;
	size_t	i;
	char	*full;

	clen = ft_strlen(cmd);
	full = (char *)malloc(dlen + clen + 2);
	if (!full)
		return (NULL);
	i = 0;
	while (i < dlen)
	{
		full[i] = dir[i];
		i++;
	}
	full[dlen] = '/';
	i = 0;
	while (i < clen)
	{
		full[dlen + 1 + i] = cmd[i];
		i++;
	}
	full[dlen + 1 + clen] = '\0';
	return (full);
}

static char	*search_path(const char *path, const char *cmd)
{
	size_t	len;
	char	*full;

	while (*path)
	{
		len = 0;
		while (path[len] && path[len] != ':')
			len++;
		full = path_join(path, len, cmd);
		if (full && access(full, X_OK) == 0)
			return (full);
		free(full);
		path += len;
		if (*path == ':')
			path++;
	}
	return (NULL);
}

char	*find_command_path(char **envp, const char *cmd)
{
	char	*path_env;

	if (ft_strchr(cmd, '/'))
		return (ft_strdup(cmd));
	path_env = find_path_env(envp);
	if (!path_env)
		return (NULL);
	return (search_path(path_env, cmd));
}
