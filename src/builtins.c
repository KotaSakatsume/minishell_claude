#include <unistd.h>
#include "minishell.h"

int	is_builtin(const char *cmd)
{
	if (!ft_strncmp(cmd, "pwd", 4) || !ft_strncmp(cmd, "echo", 5)
		|| !ft_strncmp(cmd, "env", 4) || !ft_strncmp(cmd, "exit", 5)
		|| !ft_strncmp(cmd, "cd", 3) || !ft_strncmp(cmd, "export", 7)
		|| !ft_strncmp(cmd, "unset", 6))
		return (1);
	return (0);
}

static int	bi_pwd(t_shell *shell)
{
	char	cwd[4096];

	if (!getcwd(cwd, sizeof(cwd)))
	{
		perror("minishell: pwd");
		shell->last_status = 1;
		return (1);
	}
	write(STDOUT_FILENO, cwd, ft_strlen(cwd));
	write(STDOUT_FILENO, "\n", 1);
	shell->last_status = 0;
	return (0);
}

static int	bi_echo(t_shell *shell, char **argv)
{
	int	i;
	int	newline;

	i = 1;
	newline = 1;
	if (argv[i] && !ft_strncmp(argv[i], "-n", 3))
	{
		newline = 0;
		i++;
	}
	while (argv[i])
	{
		write(STDOUT_FILENO, argv[i], ft_strlen(argv[i]));
		if (argv[i + 1])
			write(STDOUT_FILENO, " ", 1);
		i++;
	}
	if (newline)
		write(STDOUT_FILENO, "\n", 1);
	shell->last_status = 0;
	return (0);
}

static int	bi_env(t_shell *shell)
{
	int	i;

	i = 0;
	while (shell->env[i])
	{
		write(STDOUT_FILENO, shell->env[i], ft_strlen(shell->env[i]));
		write(STDOUT_FILENO, "\n", 1);
		i++;
	}
	shell->last_status = 0;
	return (0);
}

int	run_builtin(t_shell *shell, char **argv)
{
	if (!ft_strncmp(argv[0], "pwd", 4))
		return (bi_pwd(shell));
	if (!ft_strncmp(argv[0], "echo", 5))
		return (bi_echo(shell, argv));
	if (!ft_strncmp(argv[0], "env", 4))
		return (bi_env(shell));
	if (!ft_strncmp(argv[0], "cd", 3))
		return (bi_cd(shell, argv));
	if (!ft_strncmp(argv[0], "export", 7))
		return (bi_export(shell, argv));
	if (!ft_strncmp(argv[0], "unset", 6))
		return (bi_unset(shell, argv));
	return (bi_exit(shell, argv));
}
