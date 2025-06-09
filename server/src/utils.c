#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>  // pour tolower
#include <time.h>

#include "../include/utils.h"
#include "../include/color.h"

#define MAX_LINE_LENGTH 1024
#define MAX_WORDS_PER_LINE 100

void broadcast_message(Player *head, const char *message, Player *ignored_player) {
	time_t now = time(NULL);
	char *time_str = ctime(&now);
	time_str[strlen(time_str) - 1] = '\0';

	while (head) {
		if(head != ignored_player) {
			send(head->fd, message, strlen(message), 0);
		}
		head = head->next;
	}

	printf(ANSI_STYLE_BOLD ANSI_COLOR_GREEN "[%s] " ANSI_COLOR_CYAN "Server broadcast a message to everyone > " ANSI_RESET_ALL ANSI_COLOR_YELLOW "%s" ANSI_RESET_ALL, time_str, message);
}

void log_message(const char *username, const char *message, const char *addr) {
	time_t now = time(NULL);
	char *time_str = ctime(&now);
	time_str[strlen(time_str) - 1] = '\0';
	printf(ANSI_STYLE_BOLD ANSI_COLOR_GREEN "[%s] " ANSI_RESET_ALL ANSI_STYLE_BOLD "%s" ANSI_COLOR_MAGENTA "@%s" ANSI_RESET_ALL ANSI_COLOR_YELLOW " > %s\n" ANSI_RESET_ALL, time_str, username, addr, message);
}

void log_server_message(const char *username, const char *message, const char*addr) {
	time_t now = time(NULL);
	char *time_str = ctime(&now);
	time_str[strlen(time_str) - 1] = '\0';
	printf(ANSI_STYLE_BOLD ANSI_COLOR_GREEN "[%s] " ANSI_COLOR_CYAN "Server send to " ANSI_RESET_ALL ANSI_STYLE_BOLD "%s" ANSI_COLOR_MAGENTA "@%s" ANSI_RESET_ALL ANSI_COLOR_YELLOW " > %s" ANSI_RESET_ALL, time_str, username, addr, message);
}

// Fonction utilitaire : copie en minuscule
void to_lowercase(const char *src, char *dest, int max_len) {
	if (!src || !dest || max_len <= 0) return;

	int i;
	for (i = 0; src[i] && i < max_len - 1; i++) {
		dest[i] = tolower((unsigned char) src[i]);
	}

	dest[i] = '\0';  // null-terminate le résultat
}

Command *parse_input(const char *input) {
    if (!input || input[0] != '/') return NULL;

    Command *cmd = malloc(sizeof(Command));
    if (!cmd) return NULL;

    cmd->command = NULL;
    cmd->params = malloc(MAX_PARAMS * sizeof(char *));
    cmd->param_count = 0;

    if (!cmd->params) {
        free(cmd);
        return NULL;
    }

    char *copy = strdup(input); // Modifiable copy
    if (!copy) {
        free(cmd->params);
        free(cmd);
        return NULL;
    }

    // Extract command (first token before space)
    char *token = strtok(copy, " ");
    if (token) {
        cmd->command = strdup(token);
        if (!cmd->command) {
            free(copy);
            free(cmd->params);
            free(cmd);
            return NULL;
        }

        // Extract parameters (everything after first space)
        token = strtok(NULL, ""); // Remainder of the string
        if (token) {
            // Check if token contains only spaces
            int only_spaces = 1;
            for (char *p = token; *p; ++p) {
                if (!isspace((unsigned char)*p)) {
                    only_spaces = 0;
                    break;
                }
            }

            if (!only_spaces) {
                char *subtoken = strtok(token, ":");
                while (subtoken && cmd->param_count < MAX_PARAMS) {
                    // Trim leading spaces
                    while (*subtoken == ' ') subtoken++;

                    // Trim trailing spaces
                    char *end = subtoken + strlen(subtoken) - 1;
                    while (end > subtoken && *end == ' ') *end-- = '\0';

                    // Only add non-empty parameters
                    if (strlen(subtoken) > 0) {
                        cmd->params[cmd->param_count] = strdup(subtoken);
                        if (!cmd->params[cmd->param_count]) {
                            // Cleanup on error
                            for (int i = 0; i < cmd->param_count; i++)
                                free(cmd->params[i]);
                            free(cmd->params);
                            free(cmd->command);
                            free(cmd);
                            free(copy);
                            return NULL;
                        }
                        cmd->param_count++;
                    }

                    subtoken = strtok(NULL, ":");
                }
            }
        }
    }

    free(copy);
    return cmd;
}

void print_command(Command *cmd) {
	if (!cmd) return;
	printf("Command: %s\n", cmd->command);
	printf("Params (%d):\n", cmd->param_count);
	for (int i = 0; i < cmd->param_count; i++) {
		printf("  - %s\n", cmd->params[i]);
	}
}

void free_command(Command *cmd) {
	if (!cmd) return;
	free(cmd->command);
	for (int i = 0; i < cmd->param_count; i++) {
		free(cmd->params[i]);
	}
	free(cmd->params);
	free(cmd);
}

void select_random_words_from_csv(const char *filename, char *word1, char *word2) {
	FILE *file = fopen(filename, "r");
	if (!file) {
		perror("Erreur lors de l'ouverture du fichier");
		exit(EXIT_FAILURE);
	}

	// Stocker toutes les lignes
	char **lines = malloc(sizeof(char *) * 1000);
	int line_count = 0;
	char buffer[MAX_LINE_LENGTH];

	while (fgets(buffer, sizeof(buffer), file)) {
		lines[line_count] = strdup(buffer);
		line_count++;
	}

	fclose(file);

	if (line_count == 0) {
		fprintf(stderr, "Fichier vide.\n");
		exit(EXIT_FAILURE);
	}

	srand(time(NULL));
	int random_line_index = rand() % line_count;

	// Récupère la ligne choisie
	char *selected_line = lines[random_line_index];
	char *words[MAX_WORDS_PER_LINE];
	int word_count = 0;

	// Tokenisation par virgule
	char *token = strtok(selected_line, ",\n");
	while (token && word_count < MAX_WORDS_PER_LINE) {
		words[word_count++] = token;
		token = strtok(NULL, ",\n");
	}

	if (word_count < 2) {
		fprintf(stderr, "Pas assez de mots dans la ligne sélectionnée.\n");
		exit(EXIT_FAILURE);
	}

	// Choisir deux mots différents au hasard
	int idx1 = rand() % word_count;
	int idx2;
	do {
		idx2 = rand() % word_count;
	} while (idx2 == idx1);

	strcpy(word1, words[idx1]);
	strcpy(word2, words[idx2]);

	// Libérer la mémoire
	for (int i = 0; i < line_count; i++) {
		free(lines[i]);
	}
	free(lines);
}