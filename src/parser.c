#include <stdlib.h>
#include <unistd.h>
#include "minishell.h"

/* 構文エラー(`>` の後に語が無い / `|` 前後の空コマンド等): status 2 */
int	parse_error(t_shell *shell)
{
	write(STDERR_FILENO,
		"minishell: syntax error near unexpected token\n", 46);
	shell->last_status = 2;
	return (-1);
}

/* WORD を argv へ移譲し、トークンを1つ進める */
static int	p_word(t_cmd *cmd, t_tok **ptoks)
{
	if (argv_push_cmd(cmd, (*ptoks)->str))
		return (-1);
	(*ptoks)->str = NULL;
	*ptoks = (*ptoks)->next;
	return (0);
}

/* リダイレクトノードを末尾に追加(target は呼び出し側で移譲) */
static int	redir_push(t_cmd *cmd, t_tok_type type, char *target)
{
	t_redir	*r;
	t_redir	*cur;

	r = (t_redir *)malloc(sizeof(t_redir));
	if (!r)
		return (-1);
	r->type = type;
	r->target = target;
	r->fd = -1;
	r->next = NULL;
	if (!cmd->redirs)
		cmd->redirs = r;
	else
	{
		cur = cmd->redirs;
		while (cur->next)
			cur = cur->next;
		cur->next = r;
	}
	return (0);
}

/* REDIR 演算子 + 直後 WORD を t_redir に。*ptoks を2つ進める */
static int	p_redir(t_shell *shell, t_cmd *cmd, t_tok **ptoks)
{
	t_tok	*op;
	t_tok	*tgt;

	op = *ptoks;
	tgt = op->next;
	if (!tgt || tgt->type != TOK_WORD)
		return (parse_error(shell));
	if (redir_push(cmd, op->type, tgt->str))
		return (-1);
	tgt->str = NULL;
	*ptoks = tgt->next;
	return (0);
}

t_cmd	*parse_tokens(t_shell *shell, t_tok *toks)
{
	t_cmd	*head;
	t_cmd	*cur;
	int		err;

	head = cmd_new();
	if (!head)
		return (NULL);
	cur = head;
	err = 0;
	while (toks && !err)
	{
		if (toks->type == TOK_WORD)
			err = p_word(cur, &toks);
		else if (toks->type == TOK_PIPE)
			err = p_pipe(shell, &cur, &toks);
		else
			err = p_redir(shell, cur, &toks);
	}
	if (err)
	{
		free_cmd(head);
		return (NULL);
	}
	return (head);
}
