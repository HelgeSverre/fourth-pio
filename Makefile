CC = cc
CFLAGS = -Wall -Wextra -O2 -std=c11

fourth: main.c fourth.c fourth.h
	$(CC) $(CFLAGS) -o $@ main.c fourth.c

demo: fourth
	./fourth --demo

clean:
	rm -f fourth

.PHONY: demo clean
