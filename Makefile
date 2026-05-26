CC = gcc
CFLAGS = -Wall -Wextra -O3 -I. -Iinclude
LIBS = -lwayland-client -lwayland-egl -lEGL -lGL -ldl -lm

TARGET = ElJuegoDeLaVida
SRCS = ElJuegoDeLaVida.c wlr-layer-shell.c xdg-shell.c src/glad.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
