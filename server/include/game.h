#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <time.h>

#include "config.h"

typedef struct Player Player;

enum game_phase { WAITING, ASSIGNING_WORDS, PLAYING, VOTING, RESULTS };

typedef struct Played_Word {
	char word[MAX_WORD];
	struct Played_Word *next;
} Played_Word;

typedef struct Game_State {
	int max_players;
	int max_rounds;
	int timing_play;
	int timing_choice;
	enum game_phase phase;
	int player_count;
	int impostor_idx;
	int current_turn;
	int current_round;
	int votes_received;
	time_t phase_start_time;
	int temps_restant;
	Played_Word *played_words; // Liste des mots joués
	char impostor_word[MAX_WORD];
	char common_word[MAX_WORD];
} Game_State;

void assign_words(Player *head, Game_State *game);
bool is_word_played(Game_State *game, const char *word);
void add_played_word(Game_State *game, const char *word);
void free_played_words(Game_State *game);
void handle_word_submission(Player *head, Game_State *game, Player *sender, const char *word);
void handle_vote(Player *head, Game_State *game, Player *voter, const char *vote);
void reset_game(Game_State *game, Player *head);

#endif