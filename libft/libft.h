#ifndef LIBFT_H
# define LIBFT_H

# include <stddef.h>

size_t	ft_strlen(const char *s);
char	*ft_strdup(const char *s);
char	*ft_strndup(const char *s, size_t n);
int		ft_strncmp(const char *s1, const char *s2, size_t n);
char	*ft_strchr(const char *s, int c);

#endif
