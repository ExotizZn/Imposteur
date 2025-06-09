#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>

#include "../include/game.h"
#include "../include/player.h"
#include "../include/utils.h"

static void send_word(Player *player, const char *word) {
	char msg[BUFFER_SIZE];
	snprintf(msg, sizeof(msg), "/assign %s\n", word);
	send(player->fd, msg, strlen(msg), 0);
	log_server_message(player->username, msg, player->addr);
}

void assign_words(Player *head, Game_State *game) {
	char common_word[MAX_WORD], impostor_word[MAX_WORD], msg[BUFFER_SIZE];
	select_random_words_from_csv("./data/words.csv", common_word, impostor_word);

	game->impostor_idx = rand() % game->player_count;

	int idx = 0;
	Player *curr = head;
	while (curr) {
		const char *word = (idx++ == game->impostor_idx) ? impostor_word : common_word;
		strncpy(curr->secret_word, word, MAX_WORD - 1);
		curr->secret_word[MAX_WORD - 1] = '\0';
		send_word(curr, word);
		curr = curr->next;
	}

	game->phase = PLAYING;
	game->current_turn = 0;
	game->current_round = 1;
	game->phase_start_time = time(NULL);

	Player *turn_player = get_player_by_index(head, game->current_turn);
	snprintf(msg, sizeof(msg), "/info GAME:%d/%d:%d:%d:%d\n", game->current_round, game->max_rounds, game->player_count, game->timing_play, game->timing_choice);
	broadcast_message(head, msg, NULL);
	
	snprintf(msg, BUFFER_SIZE, "/info WAIT:%s:PLAY\n", turn_player->username);
	broadcast_message(head, msg, NULL);
	
	snprintf(msg, BUFFER_SIZE, "/play %d\n", game->timing_play);
	send(turn_player->fd, msg, strlen(msg), 0);
	log_server_message(turn_player->username, msg, turn_player->addr);
	
	strcpy(game->common_word, common_word); 
	strcpy(game->impostor_word, impostor_word); 
}

bool is_word_played(Game_State *game, const char *word) {
	char lowered_word[MAX_WORD];
	to_lowercase(word, lowered_word, MAX_WORD);

	for (Played_Word *cur = game->played_words; cur; cur = cur->next) {
		char lowered[MAX_WORD];
		to_lowercase(cur->word, lowered, MAX_WORD);
		if (strcmp(lowered, lowered_word) == 0)
			return true;
	}
	return false;
}

void add_played_word(Game_State *game, const char *word) {
	Played_Word *new_word = malloc(sizeof(Played_Word));
	if (!new_word) return;

	strncpy(new_word->word, word, MAX_WORD - 1);
	new_word->word[MAX_WORD - 1] = '\0';
	new_word->next = game->played_words;
	game->played_words = new_word;
}


void free_played_words(Game_State *game) {
	Played_Word *current = game->played_words;
	while (current) {
		Played_Word *next = current->next;
		free(current);
		current = next;
	}
	game->played_words = NULL;
}

void handle_word_submission(Player *head, Game_State *game, Player *sender, const char *word) {
	Player *turn_player = get_player_by_index(head, game->current_turn);
	char msg[BUFFER_SIZE];
	
	if (sender != turn_player) {
		strcpy(msg, "/ret PLAY:102\n");
		send(sender->fd, msg, strlen(msg), 0);
		log_server_message(sender->username, msg, sender->addr);
		return;
	}

	
	// Vérifier si le mot a déjà été joué
	if (is_word_played(game, word)) {
		strcpy(msg, "/ret PLAY:103\n");
		send(sender->fd, msg, strlen(msg), 0);
		log_server_message(sender->username, msg, sender->addr);
		
		snprintf(msg, BUFFER_SIZE, "/play %d\n", game->temps_restant);
		send(sender->fd, msg, strlen(msg), 0);
		log_server_message(sender->username, msg, sender->addr);
		return;
	}

	if(strchr(word, ':') != NULL) {
		strcpy(msg, "/ret PLAY:108\n");
		send(sender->fd, msg, strlen(msg), 0);
		log_server_message(sender->username, msg, sender->addr);

		snprintf(msg, BUFFER_SIZE, "/play %d\n", game->temps_restant);
		send(sender->fd, msg, strlen(msg), 0);
		return;
	}

	// Ajouter le mot à la liste des mots joués
	add_played_word(game, word);
	strncpy(sender->submitted_words[game->current_round - 1], word, MAX_WORD - 1);

	snprintf(msg, BUFFER_SIZE,"/info SAY:%s:%s\n", sender->username, word);
	broadcast_message(head, msg, NULL);

	log_message(sender->username, word, sender->addr);

	strcpy(msg, "/ret PLAY:000\n");
	send(sender->fd, msg, strlen(msg), 0);
	log_server_message(sender->username, msg, sender->addr);

	game->phase_start_time = time(NULL);

	if (++game->current_turn >= game->player_count) {
		game->current_turn = 0;
		game->current_round++;

		if(game->current_round <= game->max_rounds) {
			snprintf(msg, sizeof(msg), "/info GAME:%d/%d:%d:%d:%d\n", game->current_round, game->max_rounds, game->player_count, game->timing_play, game->timing_choice);
			broadcast_message(head, msg, NULL);
		}
	}

	if (game->current_round > game->max_rounds) {
		game->phase = VOTING;
		game->votes_received = 0;
		game->phase_start_time = time(NULL);

		snprintf(msg, sizeof(msg), "/choice %d\n", game->timing_choice);
		broadcast_message(head, msg, NULL);
	} else {
		Player *next_turn = get_player_by_index(head, game->current_turn);

		snprintf(msg, sizeof(msg), "/info WAIT:%s:PLAY\n", next_turn->username);
		broadcast_message(head, msg, NULL);

		snprintf(msg, sizeof(msg), "/play %d\n", game->timing_play);
		send(next_turn->fd, msg, strlen(msg), 0);
		log_server_message(next_turn->username, msg, next_turn->addr);

		game->phase_start_time = time(NULL);
	}
}

void handle_vote(Player *head, Game_State *game, Player *voter, const char *vote) {
	Player *target = get_player_by_username(head, vote);
	char msg[BUFFER_SIZE];
	
	if (!target) {
		strcpy(msg, "/ret CHOICE:106\n");
		send(voter->fd, msg, strlen(msg), 0);
		log_server_message(voter->username, msg, voter->addr);

		snprintf(msg, BUFFER_SIZE, "/choice %d\n", game->temps_restant);
		send(voter->fd, msg, strlen(msg), 0);
		log_server_message(voter->username, msg, voter->addr);
		return;
	}

	if(target == voter) {
		strcpy(msg, "/ret CHOICE:105\n");
		send(voter->fd, msg, strlen(msg), 0);
		log_server_message(voter->username, msg, voter->addr);

		snprintf(msg, BUFFER_SIZE, "/choice %d\n", game->temps_restant);
		send(voter->fd, msg, strlen(msg), 0);
		log_server_message(voter->username, msg, voter->addr);
		return;
	}

	strncpy(voter->vote, target->username, MAX_USERNAME - 1);
	log_message(voter->username, target->username, voter->addr);
	if(!voter->vote) {
		game->votes_received++;
	}

	char info_msg[BUFFER_SIZE];
	snprintf(info_msg, sizeof(info_msg), "/info CHOICE:%s:%s\n", voter->username, target->username);
	broadcast_message(head, info_msg, NULL);
	snprintf(msg, sizeof(msg), "/choice %d\n", game->temps_restant);
	send(voter->fd, msg, strlen(msg), 0);
	log_server_message(voter->username, msg, voter->addr);
}

void reset_game(Game_State *game, Player *head) {
	game->phase = WAITING;
	game->player_count = 0;
	game->impostor_idx = -1;
	game->current_turn = 0;
	game->current_round = 1;
	game->votes_received = 0;
	memset(game->common_word, 0, MAX_WORD);
	memset(game->impostor_word, 0, MAX_WORD);

	free_played_words(game);

	int count = 0;
	while (head) {
		head->secret_word[0] = '\0';
		for (int j = 0; j < game->max_rounds; j++)
			head->submitted_words[j][0] = '\0';
		head->vote[0] = '\0';
		head = head->next;
		count++;
	}
	game->player_count = count;
}