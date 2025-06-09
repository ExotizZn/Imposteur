#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstring>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <csignal>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "ftxui/component/captured_mouse.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/table.hpp"
#include "ftxui/screen/screen.hpp"
#include "ftxui/screen/string.hpp"
#include "ftxui/dom/node.hpp"  
#include "ftxui/screen/color.hpp"  

#include <sstream>
#include <memory>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <deque>

using namespace std;
using namespace ftxui;

std::atomic<bool> sigint_received{false};

// Gestionnaire de signal pour SIGINT
void signal_handler(int signal) {
	if (signal == SIGINT) {
		sigint_received = true;
	}
}

// Définition de l'énumération GAME_STATE
enum GAME_STATE {
	WAITING_USERNAME,
	WAITING,
	ASSIGN,
	WAITING_TURN,
	PLAYING,
	VOTING,
	RESULT
}; 

// Structures et variables globales
struct Command {
	string command;
	vector<string> params;
};

struct GameData {
	mutex mtx;
	string server_id;
	deque<string> game_log;
	vector<tuple<string, vector<string>, string>> players;
	string current_word;
	string current_login;
	string current_player;
	string impostor_name;
	string impostor_word;
	string common_word;
	string rounds;
	int players_count = 0;
	bool game_active = false;
	GAME_STATE game_state = WAITING_USERNAME;
	std::chrono::time_point<std::chrono::steady_clock> play_start_time;
	int play_duration_seconds = 30; // Durée du timer en secondes
	bool timer_active = false;
	bool show_splash = true; // Contrôle l'affichage du splash screen
	std::chrono::time_point<std::chrono::steady_clock> splash_start_time;
};

std::string to_lowercase(const std::string& str) {
	std::string lower_str = str;
	std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(),
				[](unsigned char c) { return std::tolower(c); });
	return lower_str;
}

unique_ptr<Command> parse_input(const string& input) {
	if (input.empty() || input[0] != '/') return nullptr;

	auto cmd = make_unique<Command>();
	size_t space_pos = input.find(' ');
	if (space_pos == string::npos) {
		cmd->command = input;
		return cmd;
	}

	cmd->command = input.substr(0, space_pos);
	string rest = input.substr(space_pos + 1);
	stringstream ss(rest);
	string segment;

	while (getline(ss, segment, ':')) {
		segment.erase(segment.begin(), find_if(segment.begin(), segment.end(), [](unsigned char ch) {
			return !isspace(ch);
		}));
		segment.erase(find_if(segment.rbegin(), segment.rend(), [](unsigned char ch) {
			return !isspace(ch);
		}).base(), segment.end());

		if (!segment.empty()) {
			cmd->params.push_back(segment);
		}
	}

	return cmd;
}

int shift = 0;

int connect_to_server(const string& host, int port) {
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) return -1;

	struct hostent* server = gethostbyname(host.c_str());
	if (server == nullptr) {
		close(sockfd);
		return -1;
	}

	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
	serv_addr.sin_port = htons(port);

	if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
		close(sockfd);
		return -1;
	}

	return sockfd;
}

void send_message(int sockfd, const string& message) {
	if (!message.empty()) {
		string msg = message + "\n";
		write(sockfd, msg.c_str(), msg.size());
	}
}

int get_remaining_time(const GameData& game_data) {
	if (!game_data.timer_active) return 0;
	
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
		now - game_data.play_start_time).count();
	
	int remaining = game_data.play_duration_seconds - static_cast<int>(elapsed);
	return std::max(0, remaining); // Ne pas retourner de valeur négative
}

void handle_server_messages(int sockfd, GameData& game_data, ScreenInteractive& screen, atomic<bool>& running) {
	string recv_buffer;

	while (running) {
		char buffer[1024];
		int n = read(sockfd, buffer, sizeof(buffer));
		if (n < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue; // Attendre un peu et réessayer
			} else {
				screen.Post([&] {
					lock_guard<mutex> lock(game_data.mtx);
					game_data.game_log.push_back("Erreur de lecture socket.");
					running = false;
				});
				break;
			}
		} else if (n == 0) {
			// Connexion fermée proprement par le serveur
			screen.Post([&] {
				lock_guard<mutex> lock(game_data.mtx);
				game_data.game_log.push_back("Connexion fermée par le serveur.");
				running = false;
			});
			break;
		}

		recv_buffer.append(buffer, n);
		size_t pos = 0;

		while ((pos = recv_buffer.find('\n')) != string::npos) {
			string line = recv_buffer.substr(0, pos);
			recv_buffer.erase(0, pos + 1);

			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}
			if (line.empty()) continue;

			screen.Post([line, &game_data] {
				lock_guard<mutex> lock(game_data.mtx);
				auto cmd = parse_input(line);
				if (!cmd) return;

				if (cmd->command == "/login") {
					game_data.game_log.push_back("Veuillez vous connecter.");
					game_data.game_state = WAITING_USERNAME;
				} else if (cmd->command == "/assign") {
					game_data.game_log.push_back("Votre mot est : " + (cmd->params.empty() ? "<aucun>" : cmd->params[0]));
					game_data.game_state = ASSIGN;
					game_data.current_word = cmd->params[0];

					for (auto& player : game_data.players) {
						std::get<1>(player).clear();
					}
				} else if (cmd->command == "/play") {
					game_data.game_log.push_back("C'est à votre tour de jouer !");
					game_data.game_state = PLAYING;

					game_data.play_duration_seconds = stoi(cmd->params[0]);
					game_data.play_start_time = std::chrono::steady_clock::now();
					game_data.timer_active = true;
				} else if (cmd->command == "/choice") {
					game_data.game_log.push_back("Votez pour un imposteur.");
					game_data.game_state = VOTING;

					game_data.play_duration_seconds = stoi(cmd->params[0]);
					game_data.play_start_time = std::chrono::steady_clock::now();
					game_data.timer_active = true;
				} else if (cmd->command == "/info") {
					if (cmd->params.size() >= 2 && cmd->params[0] == "ID") {
						game_data.game_log.push_back("Nom du serveur : " + cmd->params[1]);
					} else if (cmd->params[0] == "LOGIN") {
						game_data.game_log.push_back(cmd->params[2] + " viens de se connecter (" + cmd->params[1] + ")");
						bool player_exists = false;
						for (auto& player : game_data.players) {
							if (std::get<0>(player) == cmd->params[2]) {
								player_exists = true;
								break;
							}
						}
						if (!player_exists) {
							game_data.players.emplace_back(cmd->params[2], vector<string>{}, "");
							game_data.players_count++;
						}
					} else if (cmd->params[0] == "GAME") {
						game_data.game_log.push_back("Rounds (" + cmd->params[1] + ") avec " + cmd->params[2] + " joueurs");
						game_data.rounds = cmd->params[1];
					} else if (cmd->params[0] == "WAIT") {
						game_data.game_log.push_back("C'est au tour de " + cmd->params[1] + " de mettre un mot");
						game_data.current_player = cmd->params[1];
						if(to_lowercase(cmd->params[1]) != to_lowercase(game_data.current_login)) {
							game_data.game_state = WAITING_TURN;
						}

						bool player_exists = false;
						for (auto& player : game_data.players) {
							if (std::get<0>(player) == cmd->params[1]) {
								player_exists = true;
								break;
							}
						}
						if (!player_exists) {
							game_data.players.emplace_back(cmd->params[1], vector<string>{}, "");
							game_data.players_count++;
						}
					} else if (cmd->params[0] == "SAY") {
						game_data.game_log.push_back(cmd->params[1] + " a dit " + cmd->params[2]);

						bool player_exists = false;
						for (auto& player : game_data.players) {
							if (std::get<0>(player) == cmd->params[1]) {
								player_exists = true;
								break;
							}
						}
						if (!player_exists) {
							game_data.players.emplace_back(cmd->params[1], vector<string>{}, "");
							game_data.players_count++;
						}

						for (auto& player : game_data.players) {
							if (std::get<0>(player) == cmd->params[1]) {
								std::get<1>(player).push_back(cmd->params[2]);
								break;
							}
						}
					} else if (cmd->params[0] == "CHOICE") {
						game_data.game_log.push_back(cmd->params[1] + " a voté pour " + cmd->params[2]);
					} else if (cmd->params[0] == "ANSWER") {
						game_data.game_log.push_back("L'imposteur était " + cmd->params[1] + ", son mot était '" + cmd->params[2] + "', les autres avaient '" + cmd->params[3] + "'");
						game_data.game_state = RESULT;

						game_data.impostor_name = cmd->params[1];
						game_data.impostor_word = cmd->params[2];
						game_data.common_word = cmd->params[3];

						game_data.play_duration_seconds = 60;
						game_data.play_start_time = std::chrono::steady_clock::now();
						game_data.timer_active = true;
					} else if(cmd->params[0] == "RESULT") {
						for (size_t i = 1; i < cmd->params.size(); i+=2) {
							for (auto& player : game_data.players) {
								if (std::get<0>(player) == cmd->params[i]) {
									std::get<2>(player) = cmd->params[i+1];
									break;
								}
							}
						}
					} else if (cmd->params[0] == "ALERT") {
						game_data.game_log.push_back("ALERTE: " + cmd->params[1]);
						game_data.game_state = WAITING;
					} else {}
				} else if (cmd->command == "/ret") {
					if (cmd->params[0] == "LOGIN") {
						if (cmd->params[1] == "000") {
							game_data.game_log.push_back("Connexion réussie ! Vous êtes connecté en tant que " + game_data.current_login);
							game_data.game_state = WAITING;

							bool player_exists = false;
							for (const auto& player : game_data.players) {
								if (std::get<0>(player) == game_data.current_login) {
									player_exists = true;
									break;
								}
							}
							if (!player_exists) {
								game_data.players.emplace_back(game_data.current_login, vector<string>{}, "");
								game_data.players_count++;
							}
						} else if (cmd->params[1] == "101") {
							game_data.game_log.push_back("Nom d'utilisateur déjà utilisé.");
							game_data.game_state = WAITING_USERNAME;
						} else if (cmd->params[1] == "107") {
							game_data.game_log.push_back("Nom d'utilisateur invalide.");
							game_data.game_state = WAITING_USERNAME;
						} else if (cmd->params[1] == "202") {
							game_data.game_log.push_back("Commande non attendue.");
						} else {}
					} else if (cmd->params[0] == "PLAY") {
						if (cmd->params[1] == "000") {
							game_data.game_state = WAITING_TURN;
						} else if (cmd->params[1] == "102") {
							game_data.game_log.push_back("Ce n'est pas votre tour.");
							game_data.game_state = WAITING_TURN;
						} else if (cmd->params[1] == "103") {
							game_data.game_log.push_back("Mot déjà utilisé.");
							game_data.game_state = PLAYING;
						} else if (cmd->params[1] == "108") {
							game_data.game_log.push_back("Mot invalide (contient ':').");
							game_data.game_state = PLAYING;
						} else if (cmd->params[1] == "202") {
							game_data.game_log.push_back("Commande non attendue.");
						} else {}
					} else if (cmd->params[0] == "CHOICE") {
						if (cmd->params[1] == "000") {
							game_data.game_state = VOTING;
						} else if (cmd->params[1] == "105") {
							game_data.game_log.push_back("Vous ne pouvez pas voter pour vous-même.");
							game_data.game_state = VOTING;
						} else if (cmd->params[1] == "106") {
							game_data.game_log.push_back("Joueur inconnu.");
							game_data.game_state = VOTING;
						} else if (cmd->params[1] == "202") {
							game_data.game_log.push_back("Commande non attendue.");
						} else {}
					} else if (cmd->params[0] == "PROTO" && cmd->params[1] == "201") {
						game_data.game_log.push_back("Commande inconnue.");
					} else {}
				} else {
					game_data.game_log.push_back("Commande inconnue : " + cmd->command);
				}
			});
		}
	}
}

string join(const string& separator, const vector<string>& items) {
	if (items.empty()) return "";
	string result = items[0];
	for (size_t i = 1; i < items.size(); ++i) {
		result += separator + items[i];
	}
	return result;
}

int main(int argc, char* argv[]) {
	string server_ip = "127.0.0.1";
	int port = 5000;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
			server_ip = argv[++i];
		} else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
			port = stoi(argv[++i]);
		}
	}

	int sockfd = connect_to_server(server_ip, port);
	if (sockfd < 0) {
		cerr << "Error connecting to server" << endl;
		return 1;
	}

	int flags = fcntl(sockfd, F_GETFL, 0);
	fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

	GameData game_data;
	atomic<bool> running{true};

	signal(SIGINT, signal_handler);

	thread splash_timer([&] {
		this_thread::sleep_for(chrono::seconds(5));
		lock_guard<mutex> lock(game_data.mtx);
		game_data.show_splash = false;
	});

	auto screen = ScreenInteractive::ScreenInteractive::Fullscreen();
	string login_input;
	string word_input;
	string choice_input;

	auto on_click = [&] {
		lock_guard<mutex> lock(game_data.mtx);
		if (game_data.game_state == WAITING_USERNAME && !login_input.empty()) {
			send_message(sockfd, "/login " + login_input);
			game_data.current_login = login_input;
			login_input.clear();
		} else if (game_data.game_state == PLAYING && !word_input.empty()) {
			send_message(sockfd, "/play " + word_input);
			game_data.game_log.push_back(word_input);
			word_input.clear();
		} else if (game_data.game_state == VOTING && !choice_input.empty()) {
			send_message(sockfd, "/choice " + choice_input);
			choice_input.clear();
		}
	};

	auto spinner_tab_renderer = Renderer([&] {
		Elements entries;
		entries.push_back(spinner(16, shift / 2) | bold | size(WIDTH, GREATER_THAN, 2) | border | flex);
		entries.push_back(spinner(16, shift / 2) | bold | size(WIDTH, GREATER_THAN, 2) | border | flex);
		entries.push_back(spinner(16, shift / 2) | bold | size(WIDTH, GREATER_THAN, 2) | border | flex);
		entries.push_back(spinner(16, shift / 2) | bold | size(WIDTH, GREATER_THAN, 2) | border | flex);
		entries.push_back(spinner(16, shift / 2) | bold | size(WIDTH, GREATER_THAN, 2) | border | flex);
		entries.push_back(spinner(16, shift / 2) | bold | size(WIDTH, GREATER_THAN, 2) | border | flex);
		entries.push_back(spinner(16, shift / 2) | bold | size(WIDTH, GREATER_THAN, 2) | border | flex);
		entries.push_back(spinner(16, shift / 2) | bold | size(WIDTH, GREATER_THAN, 2) | border | flex);
		entries.push_back(spinner(16, shift / 2) | bold | size(WIDTH, GREATER_THAN, 2) | border | flex);
		entries.push_back(spinner(16, shift / 2) | bold | size(WIDTH, GREATER_THAN, 2) | border | flex);
		entries.push_back(spinner(16, shift / 2) | bold | size(WIDTH, GREATER_THAN, 2) | border | flex);
		return hbox({
			std::move(entries)
		}) | flex;
	});

		auto spinner_tab_renderer_info = Renderer([&] {
		Elements entries;
		entries.push_back(spinner(15, shift / 2) | bold | size(WIDTH, GREATER_THAN, 2));
		return hbox({
			std::move(entries)
		});
	});

	auto spinner_tab_renderer_splash = Renderer([&] {
		Elements entries;
		entries.push_back(spinner(21, shift / 2) | bold | size(WIDTH, GREATER_THAN, 2));
		return hbox({
			std::move(entries)
		});
	});

	auto splash_component1 = Renderer([&] {
        auto content = vbox({
            text(" ██▓ ███▄ ▄███▓ ██▓███   ▒█████    ██████ ▄▄▄█████▓▓█████  █    ██  ██▀███  ") | color(LinearGradient().Angle(45).Stop(0x1a2a6c_rgb).Stop(0xb21f1f_rgb).Stop(0xfdbb2d_rgb)),
            text("▓██▒▓██▒▀█▀ ██▒▓██░  ██▒▒██▒  ██▒▒██    ▒ ▓  ██▒ ▓▒▓█   ▀  ██  ▓██▒▓██ ▒ ██▒") | color(LinearGradient().Angle(45).Stop(0x1a2a6c_rgb).Stop(0xb21f1f_rgb).Stop(0xfdbb2d_rgb)),
            text("▒██▒▓██    ▓██░▓██░ ██▓▒▒██░  ██▒░ ▓██▄   ▒ ▓██░ ▒░▒███   ▓██  ▒██░▓██ ░▄█ ▒") | color(LinearGradient().Angle(45).Stop(0x1a2a6c_rgb).Stop(0xb21f1f_rgb).Stop(0xfdbb2d_rgb)),
            text("░██░▒██    ▒██ ▒██▄█▓▒ ▒▒██   ██░  ▒   ██▒░ ▓██▓ ░ ▒▓█  ▄ ▓▓█  ░██░▒██▀▀█▄  ") | color(LinearGradient().Angle(45).Stop(0x1a2a6c_rgb).Stop(0xb21f1f_rgb).Stop(0xfdbb2d_rgb)),
            text("░██░▒██▒   ░██▒▒██▒ ░  ░░ ████▓▒░▒██████▒▒  ▒██▒ ░ ░▒████▒▒▒█████▓ ░██▓ ▒██▒") | color(LinearGradient().Angle(45).Stop(0x1a2a6c_rgb).Stop(0xb21f1f_rgb).Stop(0xfdbb2d_rgb)),
            text("░▓  ░ ▒░   ░  ░▒▓▒░ ░  ░░ ▒░▒░▒░ ▒ ▒▓▒ ▒ ░  ▒ ░░   ░░ ▒░ ░░▒▓▒ ▒ ▒ ░ ▒▓ ░▒▓░") | color(LinearGradient().Angle(45).Stop(0x1a2a6c_rgb).Stop(0xb21f1f_rgb).Stop(0xfdbb2d_rgb)),
            text(" ▒ ░░  ░      ░░▒ ░       ░ ▒ ▒░ ░ ░▒  ░ ░    ░     ░ ░  ░░░▒░ ░ ░   ░▒ ░ ▒░") | color(LinearGradient().Angle(45).Stop(0x1a2a6c_rgb).Stop(0xb21f1f_rgb).Stop(0xfdbb2d_rgb)),
            text(" ▒ ░░      ░   ░░       ░ ░ ░ ▒  ░  ░  ░    ░         ░    ░░░ ░ ░   ░░   ░ ") | color(LinearGradient().Angle(45).Stop(0x1a2a6c_rgb).Stop(0xb21f1f_rgb).Stop(0xfdbb2d_rgb)),
            text(" ░         ░                ░ ░        ░              ░  ░   ░        ░     ") | color(LinearGradient().Angle(45).Stop(0x1a2a6c_rgb).Stop(0xb21f1f_rgb).Stop(0xfdbb2d_rgb)),
            separator(),
            text("Chargement...") | dim
        }) | center;

        return vbox({
			filler(),
            content,
            filler() // Étend le conteneur pour remplir l'espace vertical
        }) | flex | bgcolor(LinearGradient().Angle(45).Stop(0x141E30_rgb).Stop(0x243B55_rgb));
    });

	auto splash_component = Renderer([&] {
        auto content = vbox({
			hbox({
				vbox({
					text("██ ███    ███ ██████   ██████  ███████ ████████ ███████ ██    ██ ██████  ") | color(LinearGradient().Angle(45).Stop(0x1a2a6c_rgb).Stop(0xb21f1f_rgb).Stop(0xfdbb2d_rgb)),
					text("██ ████  ████ ██   ██ ██    ██ ██         ██    ██      ██    ██ ██   ██ ") | color(LinearGradient().Angle(45).Stop(0x1a2a6c_rgb).Stop(0xb21f1f_rgb).Stop(0xfdbb2d_rgb)),
					text("██ ██ ████ ██ ██████  ██    ██ ███████    ██    █████   ██    ██ ██████ ") | color(LinearGradient().Angle(45).Stop(0x1a2a6c_rgb).Stop(0xb21f1f_rgb).Stop(0xfdbb2d_rgb)),
					text("██ ██  ██  ██ ██      ██    ██      ██    ██    ██      ██    ██ ██   ██ ") | color(LinearGradient().Angle(45).Stop(0x1a2a6c_rgb).Stop(0xb21f1f_rgb).Stop(0xfdbb2d_rgb)),
					text("██ ██      ██ ██       ██████  ███████    ██    ███████  ██████  ██   ██ ") | color(LinearGradient().Angle(45).Stop(0x1a2a6c_rgb).Stop(0xb21f1f_rgb).Stop(0xfdbb2d_rgb))
				}),
				spinner_tab_renderer_splash->Render(),
			}),
            separator(),
            text("Made by Naman.") | dim,
        }) | center;

        return vbox({
			filler(),
            content,
            filler() // Étend le conteneur pour remplir l'espace vertical
        }) | flex | bgcolor(LinearGradient().Angle(45).Stop(0x141E30_rgb).Stop(0x243B55_rgb));
    });

	Component login_component = Container::Vertical({
		Input(&login_input, " Entrez votre login"),
		Button(" Rejoindre la partie", on_click) | color(LinearGradient().Angle(45).Stop(Color::Green).Stop(Color::DeepSkyBlue1)),
	});

	Component play_component = Container::Vertical({
		Input(&word_input, " Entrer un mot lié à votre mot"),
		Button(" Envoyer", on_click) | color(LinearGradient().Angle(45).Stop(Color::Green).Stop(Color::DeepSkyBlue1))
	});

	Component choice_component = Container::Vertical({
		Input(&choice_input, " Qui est l'imposteur ?"),
		Button(" Accuse", on_click) | color(LinearGradient().Angle(45).Stop(Color::Green).Stop(Color::DeepSkyBlue1))
	});

	Component main_container = Container::Vertical({
		login_component,
		play_component,
		choice_component
	});

	auto root_container = Container::Vertical({
		splash_component,
		main_container
	});

	root_container = root_container | CatchEvent([&](Event event) {
		if (event == Event::CtrlC || sigint_received) {
			running = false;
			screen.Exit();
			if (sockfd != -1) {
				close(sockfd);
				sockfd = -1;
			}
			return true;
		}
		return false;
	});

	auto renderer = Renderer(root_container, [&] {
		lock_guard<mutex> lock(game_data.mtx);
		
		// Vérifier si le splash screen doit être affiché
		if (game_data.show_splash) {
			return splash_component->Render();
		}

		// Calculer le temps restant et la progression
		int remaining_time = get_remaining_time(game_data);
		float progress = 1.0f - (static_cast<float>(remaining_time) / game_data.play_duration_seconds);
		
		// Vérifier si le temps est écoulé
		if (game_data.timer_active && remaining_time <= 0) {
			game_data.timer_active = false;
			// Ajouter une action si nécessaire quand le timer expire
			game_data.game_log.push_back("Temps écoulé !");
		}

		Elements log_entries;
		for (const auto& entry : game_data.game_log) {
			log_entries.push_back(text(entry));
		}

		Table players_table;
		vector<vector<string>> players_data;
		players_data.push_back({ " Joueur", " Mots", " Score   " });

		for (const auto& player : game_data.players) {
			string words = std::get<1>(player).empty() ? " -" : join(", ", std::get<1>(player));
			string score = std::get<2>(player).empty() ? " 0" : " " + std::get<2>(player); // Affiche "0" au lieu de vide
			players_data.push_back({ " " + std::get<0>(player), words, score });
		}

		players_table = Table(players_data);
		players_table.SelectAll().Border(LIGHT);
		players_table.SelectColumn(0).Border(DOUBLE);
		players_table.SelectColumn(1).Border(DOUBLE);
		players_table.SelectColumn(2).Border(DOUBLE);
		players_table.SelectColumn(0).Decorate(flex);
		players_table.SelectColumn(1).Decorate(flex);
		players_table.SelectRow(0).Decorate(bold);
		players_table.SelectRow(0).SeparatorVertical(LIGHT);
		players_table.SelectRow(0).Border(DOUBLE);
		
		auto content = players_table.SelectRows(1, -1);
		content.DecorateCellsAlternateRow(color(Color::Blue), 3, 0);
		content.DecorateCellsAlternateRow(color(Color::Cyan), 3, 1);
		content.DecorateCellsAlternateRow(color(Color::White), 3, 2);

		auto info_panel = vbox({
			window(text(" Informations du jeu "), 
			hbox({
				text(" Moi : " + game_data.current_login) | bgcolor(Color(Color::Black)),
				text(" | Rounds : " + game_data.rounds ) | bgcolor(Color(Color::Black)),
				text(" | Votre mot secret : " + game_data.current_word) | bgcolor(Color(Color::Black)),
				text(" | C'est au tour de : " + game_data.current_player + " ") | bgcolor(Color(Color::Black)),
				game_data.game_state == RESULT ? text(" | L'imposteur était " + game_data.impostor_name + "son mot était " + game_data.impostor_word + ". Les autres avaient le mot " + game_data.common_word) | bgcolor(Color(Color::Black)) : text(""),
				filler(),
				spinner_tab_renderer_info->Render()
			}))
		}) | bgcolor(LinearGradient().Angle(45).Stop(0x1a2a6c_rgb).Stop(0xb21f1f_rgb).Stop(0xfdbb2d_rgb));

		auto players_panel = window(text(" Joueurs "), players_table.Render() | flex);
		auto game_log_panel = window(text(" Evenements "), vbox(log_entries) | yframe | flex);
		auto top_panel = hbox({players_panel | flex/* game_log_panel | flex */}) | flex | bgcolor(LinearGradient().Angle(45).Stop(0x141E30_rgb).Stop(0x243B55_rgb));

		Component input_component;
		string input_title;

		switch(game_data.game_state) {
			case WAITING_USERNAME:
				input_title = " Connexion ";
				input_component = login_component;
				break;
			case WAITING:
				input_title = " En attente de joueurs ";
				input_component = Renderer([] { return text("En attente de joueurs..."); });
				break;
			case PLAYING:
				input_title = " C'est à votre tour ";
				input_component = play_component;
				break;
			case VOTING:
				input_title = " Choisissez l'imposteur ";
				input_component = choice_component;
				break;
			case RESULT:
				input_title = " Résultats ";
				input_component = Renderer([&] { 
					return text("L'imposteur était " + game_data.impostor_name + ", son mot était " + game_data.impostor_word + ". Les autres avaient le mot " + game_data.common_word) | blink; 
				});
				break;
			default:
				input_title = " Jeu en cours ";
				input_component = spinner_tab_renderer;
				break;
		}

		Elements input_elements;
		input_elements.push_back(input_component->Render());

		auto input_panel = window(text(input_title), vbox(move(input_elements)));

		return vbox({
			info_panel,
			top_panel,
			game_data.game_state == PLAYING || game_data.game_state == VOTING ? hbox({
				text(" Temps restant "+ to_string(remaining_time) + "s : "),
				gauge(progress) | color(Color::Cyan) |flex
			}) : text(""),
			game_data.game_state == RESULT ? hbox({
				text(" Prochaine partie dans "+ to_string(remaining_time) + "s : "),
				gauge(progress) | color(Color::Cyan) | flex
			}) : text(""),
			input_panel,
			hbox({
				text(" Appuyez sur ") | dim,
				text("Ctrl+C") | color(Color::Red),
				text(" pour quitter") | dim,
				filler(),
				text("Imposteur client V0.3") | color(LinearGradient().Angle(45).Stop(0x1a2a6c_rgb).Stop(0xb21f1f_rgb).Stop(0xfdbb2d_rgb)),
				filler(),
				text("Made by Naman.") | color(Color::Green) | blink
			})
		});
	});

	auto screenRedraw = std::thread([&](){
		while(running && !sigint_received){
			screen.PostEvent(ftxui::Event::Custom);
			screen.Post([&] { shift++; });
			std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Prevent High CPU Usage.
		}
	});

	thread server_thread(handle_server_messages, sockfd, ref(game_data), ref(screen), ref(running));
	screen.Loop(renderer);
	
	running = false;
	sigint_received = true;

	if (sockfd != -1) {
		close(sockfd);
	}

	if (server_thread.joinable()) {
		server_thread.join();
	}
	if (screenRedraw.joinable()) {
		screenRedraw.join();
	}
	if (splash_timer.joinable()) {
		splash_timer.join();
	}

	return 0;
}