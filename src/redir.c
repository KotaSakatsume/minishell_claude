#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include "minishell.h"

/* open 失敗時の bash 風メッセージ(errno 分岐) */
static void	redir_error(const char *target)
{
	char	*msg;

	if (errno == ENOENT)
		msg = "No such file or directory";
	else if (errno == EACCES)
		msg = "Permission denied";
	else if (errno == EISDIR)
		msg = "Is a directory";
	else
		msg = strerror(errno);
	write(STDERR_FILENO, "minishell: ", 11);
	write(STDERR_FILENO, target, ft_strlen(target));
	write(STDERR_FILENO, ": ", 2);
	write(STDERR_FILENO, msg, ft_strlen(msg));
	write(STDERR_FILENO, "\n", 1);
}

static int	open_redir(t_redir *r)
{
	int	fd;

	if (r->type == TOK_IN)
		fd = open(r->target, O_RDONLY);
	else if (r->type == TOK_APPEND)
		fd = open(r->target, O_WRONLY | O_CREAT | O_APPEND, 0644);
	else
		fd = open(r->target, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		redir_error(r->target);
	return (fd);
}

/* 1つのリダイレクトを適用(open or heredoc fd を dup2 し元 fd を close) */
static int	apply_one(t_redir *r)
{
	int	fd;

	if (r->type == TOK_HEREDOC)
		fd = r->fd;
	else
		fd = open_redir(r);
	if (fd < 0)
		return (-1);
	if (r->type == TOK_IN || r->type == TOK_HEREDOC)
		dup2(fd, STDIN_FILENO);
	else
		dup2(fd, STDOUT_FILENO);
	if (r->type == TOK_HEREDOC)
		r->fd = -1;
	close(fd);
	return (0);
}

int	apply_redirs(t_redir *r)
{
	while (r)
	{
		if (apply_one(r) != 0)
			return (-1);
		r = r->next;
	}
	return (0);
}

int	restore_fds(int *saved)
{
	dup2(saved[0], STDIN_FILENO);
	dup2(saved[1], STDOUT_FILENO);
	close(saved[0]);
	close(saved[1]);
	return (0);
}
