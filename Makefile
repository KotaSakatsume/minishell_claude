NAME        = minishell

CC          = cc
CFLAGS      = -Wall -Wextra -Werror -Iinclude -I$(LIBFT_DIR)

# Homebrew の readline は keg-only。brew があればパスを動的解決し、
# 無ければ(Linux/42校内環境)標準パスの -lreadline にフォールバックする。
READLINE_DIR := $(shell brew --prefix readline 2>/dev/null)
ifneq ($(READLINE_DIR),)
	CFLAGS  += -I$(READLINE_DIR)/include
	LDFLAGS += -L$(READLINE_DIR)/lib
endif
LDLIBS      = -lreadline

LIBFT_DIR   = libft
LIBFT       = $(LIBFT_DIR)/libft.a

SRCS        = src/main.c src/repl.c src/process_line.c \
              src/tokenize.c src/builtins.c src/builtin_exit.c \
              src/execute.c src/path_utils.c \
              src/env_utils.c src/env_set.c \
              src/builtin_cd.c src/builtin_export.c src/builtin_unset.c \
              src/strbuf.c src/expand.c src/lexer_state.c src/lexer.c
OBJS        = $(SRCS:.c=.o)

all: $(NAME)

$(NAME): $(LIBFT) $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LIBFT) $(LDFLAGS) $(LDLIBS) -o $(NAME)

$(LIBFT):
	$(MAKE) -C $(LIBFT_DIR)

src/%.o: src/%.c include/minishell.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)
	$(MAKE) -C $(LIBFT_DIR) clean

fclean: clean
	rm -f $(NAME)
	$(MAKE) -C $(LIBFT_DIR) fclean

re: fclean all

.PHONY: all clean fclean re
