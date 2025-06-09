#include <poll.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <signal.h>

#include "../include/game.h"
#include "../include/player.h"
#include "../include/utils.h"
#include "../include/config.h"
#include "../include/color.h"

#define HEADER \
".___                               __                       \n" \
"|   | _____ ______   ____  _______/  |_  ____  __ _________ \n" \
"|   |/     \\____  \\ /  _ \\/  ___/\\   __\\/ __ \\|  |  \\_  __ \\ \n" \
"|   |  Y Y  \\  |_> >  <_> )___ \\  |  | \\  ___/|  |  /|  | \\/\n" \
"|___|__|_|  /   __/ \\____/____  > |__|  \\___  >____/ |__|    by Naman.\n" \
"          \\/|__|              \\/            \\/              \n\n" \

static int server_fd = -1;
static struct pollfd *pollfds = NULL;
static Player *players = NULL;
static Game_State game;

static char msg_buffer[BUFFER_SIZE];
static char temp_buffer[BUFFER_SIZE];

// Handler pour nettoyage propre à l'arrêt
void cleanup_handler(int sig) {
	printf("\nArrêt du serveur...\n");
	if (pollfds) free(pollfds);
	if (server_fd >= 0) close(server_fd);
	exit(0);
}

// Optimisation: fonction inline pour éviter les appels répétés
static inline time_t get_current_time() {
	return time(NULL);
}

// Optimisation: cache pour éviter les recalculs répétés
static inline int calculate_remaining_time(time_t start_time, int duration) {
	return duration - (int)(get_current_time() - start_time);
}

// Fonction optimisée pour la gestion des votes
static void process_voting_results(Player *players, Game_State *game) {
	static int counts[MAX_PLAYERS]; // Statique pour éviter la réallocation
	static int old_scores[MAX_PLAYERS];
	static int gains[MAX_PLAYERS];
	
	// Reset des compteurs
	memset(counts, 0, sizeof(counts));
	memset(gains, 0, sizeof(gains));
	
	// Compter les votes de manière optimisée
	int player_idx = 0;
	for (Player *curr = players; curr; curr = curr->next) {
		old_scores[player_idx++] = curr->score;
		
		if (curr->vote[0] == '\0') continue; // Vote vide
		
		Player *voted = get_player_by_username(players, curr->vote);
		if (!voted) continue;

		// Trouver l'index du joueur voté
		int voted_idx = 0;
		for (Player *temp = players; temp && temp != voted; temp = temp->next) {
			voted_idx++;
		}
		counts[voted_idx]++;
	}

	// Trouver le joueur le plus voté
	int max_votes = 0, voted_idx = -1;
	for (int i = 0; i < game->player_count; i++) {
		if (counts[i] > max_votes) {
			max_votes = counts[i];
			voted_idx = i;
		}
	}

	Player *impostor_player = get_player_by_index(players, game->impostor_idx);
	Player *voted_player = get_player_by_index(players, voted_idx);

	// Mise à jour optimisée des scores
	int idx = 0;
	if (voted_idx == game->impostor_idx) {
		// L'imposteur a été démasqué
		for (Player *curr = players; curr; curr = curr->next, idx++) {
			if (curr != impostor_player) {
				curr->score += 2;
				gains[idx] = 2;
			}
		}
	} else {
		// L'imposteur n'a pas été démasqué
		for (Player *curr = players; curr; curr = curr->next, idx++) {
			if (curr == impostor_player) {
				curr->score += 3;
				gains[idx] = 3;
				break; // Optimisation: sortir dès qu'on trouve l'imposteur
			}
		}
	}

	// Construction optimisée du message RESULT
	char *ptr = msg_buffer;
	ptr += sprintf(ptr, "/info RESULT:");
	
	idx = 0;
	for (Player *curr = players; curr; curr = curr->next, idx++) {
		int remaining = BUFFER_SIZE - (ptr - msg_buffer);
		if (remaining < MAX_USERNAME + 20) break; // Protection contre overflow
		
		ptr += snprintf(ptr, remaining, "%s:%d+%d", curr->username, old_scores[idx], gains[idx]);
		if (curr->next) *ptr++ = ':';
	}
	*ptr++ = '\n';
	*ptr = '\0';

	// Envoyer les résultats
	snprintf(temp_buffer, sizeof(temp_buffer), "/info ANSWER:%s:%s:%s\n", impostor_player->username, game->impostor_word, game->common_word);
	broadcast_message(players, temp_buffer, NULL);
	broadcast_message(players, msg_buffer, NULL);
}

// Fonction optimisée pour gérer les nouvelles connexions
static int handle_new_connection(int server_fd, struct pollfd *pollfds, int *nfds, Player **players, Game_State *game) {
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);
	int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
	
	if (client_fd < 0) {
		if (errno != EWOULDBLOCK && errno != EAGAIN) {
			perror("accept");
		}
		return -1;
	}

	if (*nfds >= game->max_players + 1) {
		close(client_fd);
		return -1;
	}

	// Optimisation: préparation de l'adresse en une seule fois
	char ip[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
	int port = ntohs(client_addr.sin_port);

	char addr[MAX_ADDR];
	snprintf(addr, MAX_ADDR, "%s:%d", ip, port);
	log_message(ANSI_COLOR_RED ANSI_STYLE_BOLD "Unknown", "Waiting for username.", addr);

	pollfds[*nfds].fd = client_fd;
	pollfds[*nfds].events = POLLIN;
	(*nfds)++;

	Player *new_p = add_player(players, client_fd, game);
	strcpy(new_p->addr, addr);
	game->player_count++;

	// Optimisation: messages pré-formatés
	static const char info_msg[] = "/info ID:Serveur Imposteur Super Cool\n";
	static const char login_msg[] = "/login\n";
	
	send(client_fd, info_msg, sizeof(info_msg) - 1, 0);
	log_server_message("Unknown", info_msg, new_p->addr);
	
	send(client_fd, login_msg, sizeof(login_msg) - 1, 0);
	log_server_message("Unknown", login_msg, new_p->addr);
	
	return client_fd;
}

// Fonction optimisée pour la gestion de la phase de jeu
static void handle_playing_phase(Player *players, Game_State *game) {
	int temps_restant = calculate_remaining_time(game->phase_start_time, game->timing_play);
	game->temps_restant = temps_restant;

	if (temps_restant <= 0) {
		game->current_turn++;
		if (game->current_turn >= game->player_count) {
			game->current_turn = 0;
			game->current_round++;
			
			if (game->current_round <= game->max_rounds) {
				snprintf(msg_buffer, BUFFER_SIZE, "/info GAME:%d/%d:%d:%d:%d\n", 
						game->current_round, game->max_rounds, game->player_count, 
						game->timing_play, game->timing_choice);
				broadcast_message(players, msg_buffer, NULL);
			}
		}

		if (game->current_round > game->max_rounds) {
			game->phase = VOTING;
			snprintf(msg_buffer, BUFFER_SIZE, "/choice %d\n", game->timing_choice);
			broadcast_message(players, msg_buffer, NULL);
			game->votes_received = 0;
			game->phase_start_time = get_current_time();
		} else {
			Player *next_turn = get_player_by_index(players, game->current_turn);
			if (next_turn) {
				snprintf(msg_buffer, BUFFER_SIZE, "/info WAIT:%s:PLAY\n", next_turn->username);
				broadcast_message(players, msg_buffer, NULL);

				snprintf(temp_buffer, BUFFER_SIZE, "/play %d\n", game->timing_play);
				send(next_turn->fd, temp_buffer, strlen(temp_buffer), 0);
				log_server_message(next_turn->username, temp_buffer, next_turn->addr);
				game->phase_start_time = get_current_time();
			}
		}
	}
}

// Fonction optimisée pour la gestion de la phase de vote
static void handle_voting_phase(Player *players, Game_State *game) {
	int temps_restant = calculate_remaining_time(game->phase_start_time, game->timing_choice);
	game->temps_restant = temps_restant;

	if (temps_restant <= 0) {
		process_voting_results(players, game);
		game->phase = RESULTS;

		sleep(TIMING_BETWEEN_GAMES);
		reset_game(game, players);

		if (game->player_count >= MIN_PLAYERS) {
			broadcast_message(players, "/info ALERT:Début de la partie ! Attribution des mots...\n", NULL);
			game->phase = ASSIGNING_WORDS;
			assign_words(players, game);
		} else {
			broadcast_message(players, "/info ALERT:En attente d'autres joueurs...\n", NULL);
		}
	}
}

int main(int argc, char *argv[]) {
	srand(time(NULL));
	int opt, nfds = 1, port = DEFAULT_PORT;
	struct sockaddr_in addr;
	bool debug = false;

	// Installation du handler de signal pour cleanup
	signal(SIGINT, cleanup_handler);
	signal(SIGTERM, cleanup_handler);

	// Initialisation optimisée de la structure du jeu
	game = (Game_State){
		.max_players = DEFAULT_MAX_PLAYERS,
		.max_rounds = DEFAULT_MAX_ROUNDS,
		.timing_play = DEFAULT_TIMING_PLAY,
		.timing_choice = DEFAULT_TIMING_CHOICE,
		.phase = WAITING,
		.player_count = 0,
		.impostor_idx = -1,
		.current_turn = 0,
		.current_round = 1,
		.votes_received = 0,
		.phase_start_time = 0,
		.temps_restant = 0,
		.played_words = NULL,
		.common_word = {0},
		.impostor_word = {0}
	};

	// Parsing des arguments optimisé avec validation anticipée
	while ((opt = getopt(argc, argv, "p:j:r:t:T:d")) != -1) {
		switch (opt) {
			case 'p':
				port = atoi(optarg);
				if (port <= 0 || port > 65535) {
					fprintf(stderr, "Erreur : le port %s n'est pas valide (doit être entre 1 et 65535)\n", optarg);
					exit(EXIT_FAILURE);
				}
				break;
			case 'j':
				game.max_players = atoi(optarg);
				if (game.max_players < MIN_PLAYERS) {
					fprintf(stderr, "Erreur : le nombre maximal de joueurs doit être au moins %d\n", MIN_PLAYERS);
					exit(EXIT_FAILURE);
				}
				break;
			case 'r':
				game.max_rounds = atoi(optarg);
				if (game.max_rounds < 1) {
					fprintf(stderr, "Erreur : le nombre de rounds doit être au moins 1\n");
					exit(EXIT_FAILURE);
				}
				break;
			case 't':
				game.timing_play = atoi(optarg);
				if (game.timing_play < 1) {
					fprintf(stderr, "Erreur : TIMING_PLAY doit être positif\n");
					exit(EXIT_FAILURE);
				}
				break;
			case 'T':
				game.timing_choice = atoi(optarg);
				if (game.timing_choice < 1) {
					fprintf(stderr, "Erreur : TIMING_CHOICE doit être positif\n");
					exit(EXIT_FAILURE);
				}
				break;
			case 'd':
				debug = true;
				break;
			default:
				fprintf(stderr, "Usage: %s [-p port] [-j max_players] [-r max_rounds] [-t TIMING_PLAY] [-T TIMING_CHOICE] [-d]\n", argv[0]);
				exit(EXIT_FAILURE);
		}
	}

	// Affichage du header et des paramètres
	printf("%s", ANSI_STYLE_BOLD HEADER ANSI_RESET_ALL);

	if (debug) {
		printf(ANSI_COLOR_RED ANSI_STYLE_BOLD "[DEBUG mode activé]" ANSI_RESET_ALL "\n\n");
	}

	printf(ANSI_STYLE_BOLD ANSI_STYLE_UNDERLINE ANSI_COLOR_CYAN "Paramètres de la partie :" ANSI_RESET_ALL "\n");
	printf(ANSI_COLOR_YELLOW "● Nombre de joueurs    " ANSI_COLOR_WHITE "▸ " ANSI_STYLE_BOLD ANSI_COLOR_GREEN "%d\n" ANSI_RESET_ALL, game.max_players);
	printf(ANSI_COLOR_YELLOW "● Nombre de rounds     " ANSI_COLOR_WHITE "▸ " ANSI_STYLE_BOLD ANSI_COLOR_GREEN "%d\n" ANSI_RESET_ALL, game.max_rounds);
	printf(ANSI_COLOR_YELLOW "● TIMING_PLAY (sec)    " ANSI_COLOR_WHITE "▸ " ANSI_STYLE_BOLD ANSI_COLOR_GREEN "%d\n" ANSI_RESET_ALL, game.timing_play);
	printf(ANSI_COLOR_YELLOW "● TIMING_CHOICE (sec)  " ANSI_COLOR_WHITE "▸ " ANSI_STYLE_BOLD ANSI_COLOR_GREEN "%d\n\n" ANSI_RESET_ALL, game.timing_choice);

	// Allocation optimisée avec vérification d'erreur
	pollfds = calloc(game.max_players + 1, sizeof(struct pollfd));
	if (!pollfds) {
		perror("calloc pollfds");
		exit(EXIT_FAILURE);
	}

	// Création et configuration du socket
	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	// Optimisation: réutilisation d'adresse
	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		perror("setsockopt");
	}

	// Configuration de l'adresse
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		perror(ANSI_COLOR_RED ANSI_STYLE_BOLD "Bind " ANSI_RESET_ALL);
		exit(EXIT_FAILURE);
	}

	if (listen(server_fd, SOMAXCONN) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}

	pollfds[0].fd = server_fd;
	pollfds[0].events = POLLIN;
	printf(ANSI_COLOR_GREEN "Serveur en attente de connexion sur le port " ANSI_STYLE_BOLD "%d" ANSI_RESET_ALL "\n\n", port);

	// Boucle principale optimisée
	while (1) {
		int poll_result = poll(pollfds, nfds, 500);
		if (poll_result < 0) {
			if (errno == EINTR) continue; // Signal interrompu, continuer
			perror(ANSI_COLOR_RED "Poll failed " ANSI_RESET_ALL);
			break;
		}

		// Gestion des nouvelles connexions
		if (pollfds[0].revents & POLLIN) {
			handle_new_connection(server_fd, pollfds, &nfds, &players, &game);
		}

		// Gestion des phases de jeu
		switch (game.phase) {
			case PLAYING:
				handle_playing_phase(players, &game);
				break;
			case VOTING:
				handle_voting_phase(players, &game);
				break;
			default:
				break;
		}

		// Traitement optimisé des messages clients
		for (int i = 1; i < nfds; i++) {
			if (!(pollfds[i].revents & (POLLIN | POLLERR | POLLHUP))) continue;

			int client_fd = pollfds[i].fd;
			Player *p = get_player_by_fd(players, client_fd);
			if (!p) continue;

			char buffer[BUFFER_SIZE];
			int bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);

			if (bytes_received <= 0) {
				// Déconnexion client
				log_message(p->username[0] ? p->username : ANSI_COLOR_RED ANSI_STYLE_BOLD "Unknown" ANSI_RESET_ALL, 
						ANSI_COLOR_RED ANSI_STYLE_BOLD "Disconnected" ANSI_RESET_ALL, p->addr);
				
				snprintf(msg_buffer, BUFFER_SIZE, "/info ALERT:%s s'est déconnecté.\n", 
						p->username[0] ? p->username : "Unknown");
				
				remove_player(&players, client_fd, &game);
				pollfds[i] = pollfds[nfds - 1];
				nfds--;
				game.player_count--;

				broadcast_message(players, msg_buffer, NULL);
				
				if (game.phase != WAITING && game.player_count < MIN_PLAYERS) {
					broadcast_message(players, "/info ALERT:Un joueur s'est déconnecté. Le jeu a été interrompu. En attente d'autres joueurs...\n", NULL);
					reset_game(&game, players);
				}
				i--; // Ajuster l'index après suppression
				continue;
			}

			// Nettoyage optimisé du buffer
			buffer[bytes_received] = '\0';
			char *end = buffer + bytes_received - 1;
			while (end >= buffer && (*end == '\n' || *end == '\r')) {
				*end-- = '\0';
			}

			log_message(p->username[0] ? p->username : ANSI_COLOR_RED ANSI_STYLE_BOLD "Unknown" ANSI_RESET_ALL, buffer, p->addr);

			Command *command_parsed = parse_input(buffer);

			if (debug && command_parsed) {
				print_command(command_parsed);
			}

			if (!command_parsed) {
				static const char proto_error[] = "/ret PROTO:201\n";
				send(p->fd, proto_error, sizeof(proto_error) - 1, 0);
				log_server_message(p->username, proto_error, p->addr);
				continue;
			}

			// Traitement optimisé des commandes avec switch sur hash ou comparaison optimisée
			const char *cmd = command_parsed->command;
			
			if (cmd[1] == 'l' && strcmp(cmd, "/login") == 0) {
				if (!p->username_set) {
					const char *username = command_parsed->params[0];

					// Validation optimisée
					if (!username || strlen(username) < MIN_USERNAME || 
						strchr(username, ':') != NULL || 
						get_player_by_username(players, username)) {
						
						const char *error_msg = !username || strlen(username) < MIN_USERNAME || strchr(username, ':') != NULL ? "/ret LOGIN:107\n" : "/ret LOGIN:101\n";
						send(p->fd, error_msg, strlen(error_msg), 0);
						send(p->fd, "/login\n", 8, 0);
						log_server_message(p->username, error_msg, p->addr);
						free_command(command_parsed);
						continue;
					}

					strncpy(p->username, username, MAX_USERNAME - 1);
					p->username[MAX_USERNAME - 1] = '\0';
					p->username_set = true;
					p->ready = true;

					static const char success_msg[] = "/ret LOGIN:000\n";
					log_message(p->username, ANSI_COLOR_GREEN ANSI_STYLE_BOLD "Connected" ANSI_RESET_ALL, p->addr);
					send(p->fd, success_msg, sizeof(success_msg) - 1, 0);
					log_server_message(p->username, success_msg, p->addr);

					snprintf(msg_buffer, BUFFER_SIZE, "/info LOGIN:%d/%d:%s\n", 
					count_ready_players(players), game.max_players, username);
					broadcast_message(players, msg_buffer, NULL);
					
					if (game.phase == WAITING && all_players_ready(players, game.max_players)) {
						game.phase = ASSIGNING_WORDS;
						broadcast_message(players, "/info ALERT:Début de la partie ! Attribution des mots...\n", NULL);
						assign_words(players, &game);
					}
				} else {
					static const char already_logged[] = "/ret LOGIN:202\n";
					send(p->fd, already_logged, sizeof(already_logged) - 1, 0);
					log_server_message(p->username, already_logged, p->addr);
				}
			} else if (cmd[1] == 'p' && strcmp(cmd, "/play") == 0) {
				if (game.phase == PLAYING) {
					handle_word_submission(players, &game, p, command_parsed->params[0]);
				} else {
					static const char play_error[] = "/ret PLAY:202\n";
					send(p->fd, play_error, sizeof(play_error) - 1, 0);
					log_server_message(p->username, play_error, p->addr);
				}
			} else if (cmd[1] == 'c' && strcmp(cmd, "/choice") == 0) {
				if (game.phase == VOTING) {
					handle_vote(players, &game, p, command_parsed->params[0]);
				} else {
					static const char choice_error[] = "/ret CHOICE:202\n";
					send(p->fd, choice_error, sizeof(choice_error) - 1, 0);
					log_server_message(p->username, choice_error, p->addr);
				}
			} else {
				static const char proto_error[] = "/ret PROTO:201\n";
				send(p->fd, proto_error, sizeof(proto_error) - 1, 0);
				log_server_message(p->username, proto_error, p->addr);
			}
			
			free_command(command_parsed);
		}
	}

	// Nettoyage final
	free_played_words(&game);
	free(pollfds);
	close(server_fd);
	return EXIT_SUCCESS;
}