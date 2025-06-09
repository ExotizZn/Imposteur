#ifndef PLAYER_H
#define PLAYER_H

#include <stdbool.h>
#include "config.h"

typedef struct Game_State Game_State;

typedef struct Player Player;
typedef struct Player {
	int fd;
	char addr[MAX_ADDR];
	char username[MAX_USERNAME];
	bool username_set;
	char secret_word[MAX_WORD];
	char **submitted_words;
	char vote[MAX_USERNAME];
	int score;
	bool ready;
	Player *next;
} Player;

Player* add_player(Player **head, int fd, Game_State *game);
void remove_player(Player **head, int fd, Game_State *game);
Player* get_player_by_fd(Player *head, int fd);
Player* get_player_by_index(Player *head, int index);
Player* get_player_by_username(Player *head, const char *username);
int count_players(Player *head);
int count_ready_players(Player *head);
bool all_players_ready(Player *head, int ready_count);

#endif