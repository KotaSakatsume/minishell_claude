#include <unistd.h>
#include "minishell.h"

/* リダイレクトのみ(argv 空)。ファイル作成/truncate だけ行い exec しない */
static int	exec_redir_only(t_shell *shell, t_cmd *cmd)
{
	int	saved[2];
	int	r;

	saved[0] = dup(STDIN_FILENO);
	saved[1] = dup(STDOUT_FILENO);
	r = apply_redirs(cmd->redirs);
	restore_fds(saved);
	if (r != 0)
		shell->last_status = 1;
	else
		shell->last_status = 0;
	return (0);
}

/* ビルトイン: 親で標準fdを退避 → リダイレクト適用 → 実行 → 復元 */
static int	exec_builtin_redir(t_shell *shell, t_cmd *cmd)
{
	int	saved[2];

	saved[0] = dup(STDIN_FILENO);
	saved[1] = dup(STDOUT_FILENO);
	if (apply_redirs(cmd->redirs))
	{
		shell->last_status = 1;
		restore_fds(saved);
		return (1);
	}
	run_builtin(shell, cmd->argv);
	restore_fds(saved);
	return (0);
}

static int	exec_cmd(t_shell *shell, t_cmd *cmd)
{
	if (run_heredocs(shell, cmd))
		return (1);
	if (!cmd->argv[0])
		return (exec_redir_only(shell, cmd));
	if (is_builtin(cmd->argv[0]))
		return (exec_builtin_redir(shell, cmd));
	return (run_external(shell, cmd));
}

/*
** レキサ → パーサ → 実行 → 解放。空行/未閉じクォート/構文エラーは
** lex_tokens/parse_tokens が NULL を返し no-op(status は内部で設定)。
*/
int	process_line(t_shell *shell, char *line)
{
	t_tok	*toks;
	t_cmd	*cmd;

	toks = lex_tokens(shell, line);
	if (!toks)
		return (0);
	cmd = parse_tokens(shell, toks);
	free_tok(toks);
	if (!cmd)
		return (0);
	if (cmd->next)
		exec_pipeline(shell, cmd);
	else
		exec_cmd(shell, cmd);
	free_cmd(cmd);
	return (0);
}
