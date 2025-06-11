# 🎭 Imposteur
Jeu multijoueur où un imposteur doit se cacher parmi les autres joueurs qui partagent un mot commun. Le but ? Trouver l’imposteur… ou ne pas se faire démasquer !

[![forthebadge](http://forthebadge.com/images/badges/built-with-love.svg)](http://forthebadge.com) [![forthebadge](https://forthebadge.com/images/badges/made-with-c.svg)](https://forthebadge.com) [![forthebadge](https://forthebadge.com/images/badges/made-with-c-plus-plus.svg)](https://forthebadge.com)

## Pour commencer
### Prérequis
* CMake
```sh
  sudo apt install cmake
```
### Installation
1. Cloner le repo
  ```sh
    git clone https://github.com/ExotizZn/Imposteur
  ```
2. Compiler le serveur
  ```sh
    cd server
    make
  ```
3. Compiler le client
  ```sh
    cd client
    mkdir build && cd build
    cmake ..
    make
  ```

## Utilisation
Pour lancer le serveur (il faut être dans le dossier "server/")
```sh
./imposteur_server [-p PORT] [-r NB_ROUNDS] [-j NB_JOUEURS] [-t TIMING_PLAY] [-T TIMING_CHOICE] [-d]
```
- PORT : Port du serveur (par défaut : 5000)
- NB_ROUNDS : Nombre de rounds par partie (par défaut : 3)
- NB_JOUEURS : Nombre de joueurs (par défaut : 10)
- TIMING_PLAY : Nombre de secondes pour mettre un mot (par défaut : 30)
- TIMING_CHOICE : Nombre de secondes pour voter (par défaut : 60)

Pour lancer le client (il faut être dans le dossier "client/build/")
```sh
./imposteur_client [-s IP] [-p PORT]
```
- IP : IP du serveur (par défaut : 127.0.0.1)
- PORT : Port du serveur (par défaut : 5000)
