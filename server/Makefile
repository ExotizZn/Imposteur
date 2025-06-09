CC       := gcc
CFLAGS   := -O3 -Wall
SRC      := ./src
INCLUDE  := ./include
OBJFILES := imposteur_server.o utils.o player.o game.o
TARGET   := imposteur_server

all: $(TARGET) clean

${TARGET}: ${OBJFILES}
	${CC} ${OBJFILES} -o ${TARGET} ${CFLAGS}

imposteur_server.o : ${SRC}/imposteur_server.c
	${CC} -c ${SRC}/imposteur_server.c

game.o : ${SRC}/game.c
	${CC} -c ${SRC}/game.c

player.o : ${SRC}/player.c
	${CC} -c ${SRC}/player.c

utils.o : ${SRC}/utils.c
	${CC} -c ${SRC}/utils.c

clean:
	rm -f *~ *.o