#ifndef UTILS_H
#define UTILS_H

#include "player.h"

#define MAX_PARAMS 10

typedef struct Command{
    char *command;
    char **params;
    int param_count;
} Command;

void broadcast_message(Player *head, const char *message, Player *ignored_player);
void log_message(const char *username, const char *message, const char *addr);
void log_server_message(const char *username, const char *message, const char*addr);
void to_lowercase(const char *src, char *dest, int max_len); // Fonction utilitaire : copie en minuscule
Command *parse_input(const char *input);
void print_command(Command *cmd);
void free_command(Command *cmd);
void select_random_words_from_csv(const char *filename, char *word1, char *word2);

#endif