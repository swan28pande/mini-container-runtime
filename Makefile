CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra $(EXTRA_CFLAGS)
SRC     := $(wildcard src/*.c)
OBJ     := $(SRC:.c=.o)

minirun: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

src/%.o: src/%.c src/runtime.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f minirun $(OBJ)

.PHONY: clean
