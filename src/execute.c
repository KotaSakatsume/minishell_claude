#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include "minishell.h"

static void	cmd_error(const char *cmd, const char *msg)
{
	write(STDERR_FILENO, "minishell: ", 11);
	write(STDERR_FILENO, cmd, ft_strlen(cmd));
	write(STDERR_FILENO, ": ", 2);
	write(STDERR_FILENO, msg, ft_strlen(msg));
	write(STDERR_FILENO, "\n", 1);
}

/*
** 子プロセス: execve が戻った = 失敗。errno で 127/126 を判定し、
** 確保物を全 free してから exit(親の free_argv には到達しないため)。
*/
static void	child_exec(char *path, char **argv, char **envp)
{
	execve(path, argv, envp);
	if (errno == ENOENT)
	{
		cmd_error(argv[0], "command not found");
		free(path);
		free_argv(argv);
		exit(127);
	}
	cmd_error(argv[0], "Permission denied");
	free(path);
	free_argv(argv);
	exit(126);
}

static int	wait_status(int status)
{
	if (WIFEXITED(status))
		return (WEXITSTATUS(status));
	if (WIFSIGNALED(status))
		return (128 + WTERMSIG(status));
	return (0);
}

int	run_external(t_shell *shell, char **argv)
{
	char	*path;
	pid_t	pid;
	int		status;

	path = find_command_path(shell->envp, argv[0]);
	if (!path)
	{
		cmd_error(argv[0], "command not found");
		shell->last_status = 127;
		return (127);
	}
	pid = fork();
	if (pid < 0)
	{
		perror("minishell: fork");
		free(path);
		shell->last_status = 1;
		return (1);
	}
	if (pid == 0)
		child_exec(path, argv, shell->envp);
	waitpid(pid, &status, 0);
	free(path);
	shell->last_status = wait_status(status);
	return (shell->last_status);
}
