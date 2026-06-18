#include <stdlib.h>
#include <unistd.h>
#include "minishell.h"

t_cmd	*cmd_new(void)
{
	t_cmd	*cmd;

	cmd = (t_cmd *)malloc(sizeof(t_cmd));
	if (!cmd)
		return (NULL);
	cmd->argv = (char **)malloc(sizeof(char *));
	if (!cmd->argv)
	{
		free(cmd);
		return (NULL);
	}
	cmd->argv[0] = NULL;
	cmd->redirs = NULL;
	return (cmd);
}

/* cmd->argv に word を追加(自前 grow)。失敗時は word を free して -1 */
int	argv_push_cmd(t_cmd *cmd, char *word)
{
	int		n;
	int		i;
	char	**na;

	n = 0;
	while (cmd->argv[n])
		n++;
	na = (char **)malloc(sizeof(char *) * (n + 2));
	if (!na)
	{
		free(word);
		return (-1);
	}
	i = 0;
	while (i < n)
	{
		na[i] = cmd->argv[i];
		i++;
	}
	na[n] = word;
	na[n + 1] = NULL;
	free(cmd->argv);
	cmd->argv = na;
	return (0);
}

void	free_redirs(t_redir *r)
{
	t_redir	*next;

	while (r)
	{
		next = r->next;
		free(r->target);
		if (r->fd >= 0)
			close(r->fd);
		free(r);
		r = next;
	}
}

void	free_cmd(t_cmd *cmd)
{
	if (!cmd)
		return ;
	free_argv(cmd->argv);
	free_redirs(cmd->redirs);
	free(cmd);
}

void	free_tok(t_tok *t)
{
	t_tok	*next;

	while (t)
	{
		next = t->next;
		free(t->str);
		free(t);
		t = next;
	}
}
