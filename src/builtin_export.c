#include <unistd.h>
#include <stdlib.h>
#include "minishell.h"

static int	is_id_char(char c, int first)
{
	if (c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
		return (1);
	if (!first && c >= '0' && c <= '9')
		return (1);
	return (0);
}

static int	is_valid_id(const char *s)
{
	int	i;

	if (!s || !s[0] || !is_id_char(s[0], 1))
		return (0);
	i = 1;
	while (s[i] && s[i] != '=')
	{
		if (!is_id_char(s[i], 0))
			return (0);
		i++;
	}
	return (1);
}

static void	print_env_plain(t_shell *shell)
{
	int	i;

	i = 0;
	while (shell->env[i])
	{
		write(STDOUT_FILENO, shell->env[i], ft_strlen(shell->env[i]));
		write(STDOUT_FILENO, "\n", 1);
		i++;
	}
}

/*
** export NAME=VALUE を1件処理。値なし(= なし)は最小仕様で no-op。
** 不正識別子は bash 風メッセージ + status 1。一時 key は必ず free。
*/
static int	export_one(t_shell *shell, char *arg)
{
	char	*eq;
	char	*key;

	if (!is_valid_id(arg))
	{
		write(STDERR_FILENO, "minishell: export: `", 20);
		write(STDERR_FILENO, arg, ft_strlen(arg));
		write(STDERR_FILENO, "': not a valid identifier\n", 26);
		return (1);
	}
	eq = ft_strchr(arg, '=');
	if (!eq)
		return (0);
	key = ft_strndup(arg, eq - arg);
	if (!key)
		return (1);
	shell->env = env_set(shell->env, key, eq + 1);
	free(key);
	return (0);
}

int	bi_export(t_shell *shell, char **argv)
{
	int	i;
	int	status;

	if (!argv[1])
	{
		print_env_plain(shell);
		shell->last_status = 0;
		return (0);
	}
	i = 1;
	status = 0;
	while (argv[i])
	{
		if (export_one(shell, argv[i]) != 0)
			status = 1;
		i++;
	}
	shell->last_status = status;
	return (status);
}
