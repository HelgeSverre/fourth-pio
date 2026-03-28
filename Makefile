CC = cc
CFLAGS = -Wall -Wextra -O2 -std=c11

fourth: main.c fourth.c fourth.h
	$(CC) $(CFLAGS) -o $@ main.c fourth.c

tests/test_fourth: tests/test_fourth.c fourth.c fourth.h
	$(CC) $(CFLAGS) -o $@ tests/test_fourth.c fourth.c

demo: fourth
	./fourth --demo

test: tests/test_fourth
	python3 tests/test_oracle.py

clean:
	rm -f fourth tests/test_fourth

.PHONY: demo test clean
