#include <stdlib.h>
#include "minishell.h"

static int	is_space(char c)
{
	return (c == ' ' || c == '\t');
}

static int	count_words(const char *s)
{
	int	count;
	int	in_word;

	count = 0;
	in_word = 0;
	while (*s)
	{
		if (!is_space(*s) && !in_word)
		{
			in_word = 1;
			count++;
		}
		else if (is_space(*s))
			in_word = 0;
		s++;
	}
	return (count);
}

void	free_argv(char **argv)
{
	int	i;

	if (!argv)
		return ;
	i = 0;
	while (argv[i])
	{
		free(argv[i]);
		i++;
	}
	free(argv);
}

static int	fill_argv(char **argv, const char *s)
{
	int		i;
	size_t	len;

	i = 0;
	while (*s)
	{
		while (is_space(*s))
			s++;
		if (!*s)
			break ;
		len = 0;
		while (s[len] && !is_space(s[len]))
			len++;
		argv[i] = ft_strndup(s, len);
		if (!argv[i])
			return (-1);
		i++;
		s += len;
	}
	argv[i] = NULL;
	return (0);
}

char	**tokenize(const char *line)
{
	char	**argv;
	int		words;

	words = count_words(line);
	argv = (char **)malloc(sizeof(char *) * (words + 1));
	if (!argv)
		return (NULL);
	if (fill_argv(argv, line) == -1)
	{
		free_argv(argv);
		return (NULL);
	}
	return (argv);
}
