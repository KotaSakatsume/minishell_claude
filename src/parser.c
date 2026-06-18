#include <stdlib.h>
#include <unistd.h>
#include "minishell.h"

/* 構文エラー(`>` の後に語が無い等): bash 風メッセージ + status 2 */
static int	parse_error(t_shell *shell)
{
	write(STDERR_FILENO,
		"minishell: syntax error near unexpected token\n", 46);
	shell->last_status = 2;
	return (-1);
}

/* WORD を argv へ移譲 */
static int	p_word(t_cmd *cmd, t_tok *tok)
{
	if (argv_push_cmd(cmd, tok->str))
		return (-1);
	tok->str = NULL;
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
	t_cmd	*cmd;
	int		err;

	cmd = cmd_new();
	if (!cmd)
		return (NULL);
	err = 0;
	while (toks && !err)
	{
		if (toks->type == TOK_WORD)
		{
			err = p_word(cmd, toks);
			toks = toks->next;
		}
		else
			err = p_redir(shell, cmd, &toks);
	}
	if (err)
	{
		free_cmd(cmd);
		return (NULL);
	}
	return (cmd);
}
