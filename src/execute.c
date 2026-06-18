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
** 子プロセス: リダイレクト適用 → execve。失敗時は errno で 126/127 を判定し
** path を free して exit(親側 cmd は親が解放)。
*/
static void	child_exec(t_shell *shell, t_cmd *cmd, char *path)
{
	int	err;

	if (apply_redirs(cmd->redirs))
	{
		free(path);
		exit(1);
	}
	execve(path, cmd->argv, shell->env);
	err = errno;
	free(path);
	if (err == ENOENT)
	{
		cmd_error(cmd->argv[0], "command not found");
		exit(127);
	}
	cmd_error(cmd->argv[0], "Permission denied");
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

int	run_external(t_shell *shell, t_cmd *cmd)
{
	char	*path;
	pid_t	pid;
	int		status;

	path = find_command_path(shell->env, cmd->argv[0]);
	if (!path)
	{
		cmd_error(cmd->argv[0], "command not found");
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
		child_exec(shell, cmd, path);
	waitpid(pid, &status, 0);
	free(path);
	shell->last_status = wait_status(status);
	return (shell->last_status);
}
