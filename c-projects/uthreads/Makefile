CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11 -g -Iinclude

# ── source files ───────────────────────────────────────────────────────
LIB_SRCS  = src/uthreads.c src/sync.c
LIB_OBJS  = src/uthreads.o src/sync.o

DEMO_SRCS = demo/demo.c
DEMO_OBJS = demo/demo.o

SYNC_SRCS = demo/sync_demo.c
SYNC_OBJS = demo/sync_demo.o

# ── targets ────────────────────────────────────────────────────────────
DEMO_BIN      = uthreads_demo
SYNC_DEMO_BIN = sync_demo

ALL_BINS = $(DEMO_BIN) $(SYNC_DEMO_BIN)

# ── default: build everything ──────────────────────────────────────────
all: $(ALL_BINS)

$(DEMO_BIN): $(LIB_OBJS) $(DEMO_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(SYNC_DEMO_BIN): $(LIB_OBJS) $(SYNC_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# ── object rules ───────────────────────────────────────────────────────
src/uthreads.o: src/uthreads.c include/uthreads.h
	$(CC) $(CFLAGS) -c -o $@ $<

src/sync.o: src/sync.c src/uthreads_internal.h include/uthreads.h
	$(CC) $(CFLAGS) -c -o $@ $<

demo/demo.o: demo/demo.c include/uthreads.h
	$(CC) $(CFLAGS) -c -o $@ $<

demo/sync_demo.o: demo/sync_demo.c include/uthreads.h
	$(CC) $(CFLAGS) -c -o $@ $<

# ── run ────────────────────────────────────────────────────────────────
run: $(DEMO_BIN)
	./$(DEMO_BIN)

run-sync: $(SYNC_DEMO_BIN)
	./$(SYNC_DEMO_BIN)

run-all: all
	@echo "── threading demo ─────────────────────────────────"
	./$(DEMO_BIN)
	@echo ""
	@echo "── sync demo ──────────────────────────────────────"
	./$(SYNC_DEMO_BIN)

# ── clean ──────────────────────────────────────────────────────────────
clean:
	rm -f $(LIB_OBJS) $(DEMO_OBJS) $(SYNC_OBJS) $(ALL_BINS)

.PHONY: all run run-sync run-all clean
