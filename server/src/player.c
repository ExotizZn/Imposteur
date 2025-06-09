#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "../include/game.h"
#include "../include/player.h"
#include "../include/utils.h"

Player* add_player(Player **head, int fd, Game_State *game) {
	Player *new_player = malloc(sizeof(Player));
	if (!new_player) return NULL;

	new_player->fd = fd;
	new_player->username[0] = '\0';
	new_player->username_set = false;
	new_player->secret_word[0] = '\0';
	new_player->score = 0;
	new_player->ready = false;
	new_player->vote[0] = '\0';
	new_player->next = *head;

	new_player->submitted_words = malloc(game->max_rounds * sizeof(char *));
	for (int i = 0; i < game->max_rounds; i++) {
		new_player->submitted_words[i] = malloc(MAX_WORD * sizeof(char));
		new_player->submitted_words[i][0] = '\0';
	}

	*head = new_player;
	return new_player;
}

void remove_player(Player **head, int fd, Game_State *game) {
	Player *curr = *head, *prev = NULL;
	while (curr) {
		if (curr->fd == fd) {
			if (prev) {
				prev->next = curr->next;
			} else {
				*head = curr->next;
			}
			close(curr->fd);
			free(curr);
			return;
		}
		prev = curr;
		curr = curr->next;
	}
}

Player* get_player_by_fd(Player *head, int fd) {
	while (head) {
		if (head->fd == fd) return head;
		head = head->next;
	}
	return NULL;
}

Player* get_player_by_index(Player *head, int index) {
	int i = 0;
	while (head) {
		if (i == index) return head;
		head = head->next;
		i++;
	}
	return NULL;
}

Player* get_player_by_username(Player *head, const char *username) {
	char lowered_input[MAX_USERNAME];
	to_lowercase(username, lowered_input, MAX_USERNAME);

	while (head) {
		char lowered_current[MAX_USERNAME];
		to_lowercase(head->username, lowered_current, MAX_USERNAME);

		if (strcmp(lowered_current, lowered_input) == 0) {
			return head;
		}
		head = head->next;
	}
	return NULL;
}

int count_players(Player *head) {
	int count = 0;
	while (head) {
		count++;
		head = head->next;
	}
	return count;
}

int count_ready_players(Player *head) {
	int count = 0;
	while (head) {
		if(head->ready) count++;
		head = head->next;
	}
	return count;
}

bool all_players_ready(Player *head, int ready_count) {
	return count_ready_players(head) == ready_count;
}