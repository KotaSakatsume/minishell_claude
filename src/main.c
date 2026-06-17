#include "minishell.h"

int	shell_init(t_shell *shell, char **envp)
{
	shell->envp = envp;
	shell->last_status = 0;
	return (0);
}

void	shell_cleanup(t_shell *shell)
{
	shell->envp = NULL;
	shell->last_status = 0;
	rl_clear_history();
}

int	main(int argc, char **argv, char **envp)
{
	t_shell	shell;

	(void)argc;
	(void)argv;
	if (shell_init(&shell, envp) != 0)
	{
		perror("minishell: init");
		return (1);
	}
	repl_loop(&shell);
	shell_cleanup(&shell);
	return (shell.last_status);
}
