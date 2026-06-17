#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "minishell.h"

static void	cd_error(const char *arg, const char *msg)
{
	write(STDERR_FILENO, "minishell: cd: ", 15);
	write(STDERR_FILENO, arg, ft_strlen(arg));
	write(STDERR_FILENO, ": ", 2);
	write(STDERR_FILENO, msg, ft_strlen(msg));
	write(STDERR_FILENO, "\n", 1);
}

/*
** PWD/OLDPWD を更新(getcwd ベース=物理パス。macOS のシンボリックリンク配下では
** bash の論理 PWD と乖離しうる既知差分。getcwd 失敗時は best-effort で更新しない)。
*/
static void	cd_update_pwd(t_shell *shell, char *oldpwd)
{
	char	newpwd[4096];

	if (getcwd(newpwd, sizeof(newpwd)))
	{
		shell->env = env_set(shell->env, "OLDPWD", oldpwd);
		shell->env = env_set(shell->env, "PWD", newpwd);
	}
}

/*
** cd(相対/絶対パスのみ)。引数なしは no-op(HOME 移動は別Issue)。
*/
int	bi_cd(t_shell *shell, char **argv)
{
	char	oldpwd[4096];

	if (!argv[1])
	{
		shell->last_status = 0;
		return (0);
	}
	if (!getcwd(oldpwd, sizeof(oldpwd)))
		oldpwd[0] = '\0';
	if (chdir(argv[1]) != 0)
	{
		cd_error(argv[1], strerror(errno));
		shell->last_status = 1;
		return (1);
	}
	cd_update_pwd(shell, oldpwd);
	shell->last_status = 0;
	return (0);
}
