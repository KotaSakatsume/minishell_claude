#include <stdlib.h>
#include <unistd.h>
#include "minishell.h"

/* 前段読端を stdin、自段書端を stdout に接続し、不要 pipe fd を全 close */
static void	pipe_dup(int in_fd, int *pfd)
{
	if (in_fd != -1)
	{
		dup2(in_fd, STDIN_FILENO);
		close(in_fd);
	}
	if (pfd[1] != -1)
	{
		dup2(pfd[1], STDOUT_FILENO);
		close(pfd[1]);
		close(pfd[0]);
	}
}

static int	child_not_found(const char *name)
{
	write(STDERR_FILENO, "minishell: ", 11);
	write(STDERR_FILENO, name, ft_strlen(name));
	write(STDERR_FILENO, ": command not found\n", 20);
	return (127);
}

/*
** パイプライン1段の子: pipe 接続 → heredoc/redir(pipe より優先)→
** builtin は子で実行して exit、外部は execve。return しない。
*/
void	pipe_child(t_shell *shell, t_cmd *cmd, int in_fd, int *pfd)
{
	char	*path;

	pipe_dup(in_fd, pfd);
	if (run_heredocs(shell, cmd) || apply_redirs(cmd->redirs))
		exit(1);
	if (!cmd->argv[0])
		exit(0);
	if (is_builtin(cmd->argv[0]))
	{
		run_builtin(shell, cmd->argv);
		exit(shell->last_status);
	}
	path = find_command_path(shell->env, cmd->argv[0]);
	if (!path)
		exit(child_not_found(cmd->argv[0]));
	exec_external(shell, cmd, path);
}
