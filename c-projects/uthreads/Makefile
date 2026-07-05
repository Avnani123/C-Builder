CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11 -g -Iinclude
TARGET  = uthreads_demo

SRC_LIB  = src/uthreads.c
SRC_DEMO = demo/demo.c
OBJS     = src/uthreads.o demo/demo.o

# ── default: build the demo executable ────────────────────────────────
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

src/uthreads.o: src/uthreads.c include/uthreads.h
	$(CC) $(CFLAGS) -c -o $@ $<

demo/demo.o: demo/demo.c include/uthreads.h
	$(CC) $(CFLAGS) -c -o $@ $<

# ── run ────────────────────────────────────────────────────────────────
run: $(TARGET)
	./$(TARGET)

# ── clean ──────────────────────────────────────────────────────────────
clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all run clean
