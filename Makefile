CC      = gcc
CFLAGS  = -Wall -Werror -pthread -O3 -std=c11
TARGET  = eachrink

all: $(TARGET)

$(TARGET): eachrink.c
	$(CC) $(CFLAGS) -o $@ $<

test: $(TARGET)
	for i in $$(seq 2000); do echo "the quick brown fox jumps over the lazy dog $$i"; done > /tmp/eachrink_test.txt
	./$(TARGET) c /tmp/eachrink_test.txt /tmp/eachrink_test.bin 4
	./$(TARGET) d /tmp/eachrink_test.bin /tmp/eachrink_test.out
	cmp /tmp/eachrink_test.txt /tmp/eachrink_test.out && echo "Round-trip OK"

clean:
	rm -f $(TARGET)

.PHONY: all test clean
