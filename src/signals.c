#include <signal.h>
#include <unistd.h>
#include "minishell.h"

/* 唯一許可されるグローバル変数(受信シグナル番号のみ) */
sig_atomic_t	g_signal = 0;

/*
** 対話プロンプト中の SIGINT: 番号を記録し、新しい行に新プロンプトを再描画。
** async-signal-safe な write + readline 提供 API のみ(printf/malloc 不可)。
*/
static void	sigint_prompt(int sig)
{
	g_signal = sig;
	write(STDOUT_FILENO, "\n", 1);
	rl_on_new_line();
	rl_replace_line("", 0);
	rl_redisplay();
}

/* 対話モード: SIGINT=新プロンプト / SIGQUIT=無視 */
void	set_signals_interactive(void)
{
	struct sigaction	sa;

	sa.sa_handler = sigint_prompt;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGINT, &sa, NULL);
	signal(SIGQUIT, SIG_IGN);
}

/* 実行中の親: SIGINT/SIGQUIT を無視(シェルは死なない) */
void	set_signals_exec_parent(void)
{
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
}

/* fork 後の子: デフォルトに戻す(実行コマンドが ctrl-C/ctrl-\ で死ぬ) */
void	reset_signals_child(void)
{
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
}
