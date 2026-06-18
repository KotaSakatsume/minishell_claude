#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include "minishell.h"

static int	count_cmds(t_cmd *cmd)
{
	int	n;

	n = 0;
	while (cmd)
	{
		n++;
		cmd = cmd->next;
	}
	return (n);
}

/*
** 1段分: 次段があれば pipe を張り fork。子は pipe_child(return しない)。
** 親は前段読端を close し、書端を即 close、次段の読端を in_fd に持ち回る。
*/
static pid_t	pipe_fork_one(t_shell *shell, t_cmd *cmd, int *in_fd)
{
	int		pfd[2];
	pid_t	pid;

	pfd[0] = -1;
	pfd[1] = -1;
	if (cmd->next)
		pipe(pfd);
	pid = fork();
	if (pid == 0)
		pipe_child(shell, cmd, *in_fd, pfd);
	if (*in_fd != -1)
		close(*in_fd);
	if (cmd->next)
	{
		close(pfd[1]);
		*in_fd = pfd[0];
	}
	else
		*in_fd = -1;
	return (pid);
}

/* 全子を waitpid(ゾンビ防止)。最終段の status を last_status に採用 */
static void	wait_all(pid_t *pids, int n, t_shell *shell)
{
	int	status;
	int	i;

	i = 0;
	while (i < n)
	{
		waitpid(pids[i], &status, 0);
		if (i == n - 1)
			shell->last_status = wait_status(status);
		i++;
	}
}

int	exec_pipeline(t_shell *shell, t_cmd *head)
{
	pid_t	*pids;
	int		in_fd;
	int		i;
	t_cmd	*cur;

	pids = (pid_t *)malloc(sizeof(pid_t) * count_cmds(head));
	if (!pids)
		return (1);
	in_fd = -1;
	i = 0;
	cur = head;
	while (cur)
	{
		pids[i++] = pipe_fork_one(shell, cur, &in_fd);
		cur = cur->next;
	}
	if (in_fd != -1)
		close(in_fd);
	wait_all(pids, i, shell);
	free(pids);
	return (0);
}
