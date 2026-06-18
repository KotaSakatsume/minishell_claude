#include <stdlib.h>
#include <unistd.h>
#include "minishell.h"

/* 入力行が delimiter と完全一致するか */
static int	is_delim(const char *line, const char *delim)
{
	size_t	len;

	len = ft_strlen(delim);
	return (!ft_strncmp(line, delim, len) && line[len] == '\0');
}

/* delimiter まで読み、内容を pipe に書いて読み端を r->fd に保持
** (history には積まない。変数展開はしない=本Issueの最小固定) */
static int	run_one_heredoc(t_redir *r)
{
	int		p[2];
	char	*line;

	if (pipe(p) < 0)
		return (1);
	while (1)
	{
		line = readline("> ");
		if (!line || is_delim(line, r->target))
		{
			free(line);
			break ;
		}
		write(p[1], line, ft_strlen(line));
		write(p[1], "\n", 1);
		free(line);
	}
	close(p[1]);
	r->fd = p[0];
	return (0);
}

/* exec 前に全 heredoc を先読み(出現順) */
int	run_heredocs(t_shell *shell, t_cmd *cmd)
{
	t_redir	*r;

	(void)shell;
	r = cmd->redirs;
	while (r)
	{
		if (r->type == TOK_HEREDOC && run_one_heredoc(r) != 0)
			return (1);
		r = r->next;
	}
	return (0);
}
