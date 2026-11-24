# framereplay - deterministic, frame-indexed input/gesture review
#
# Headless verification builds against raylib's PLATFORM_MEMORY (software renderer, no OS
# window, no GPU). The window viewer (replay_viewer.c) is syntax-checked here and is meant
# to be built on a desktop machine with GL/GLFW using the documented command in its header.

RAYLIB_SRC ?= /workspace/src
CC         ?= gcc
CFLAGS     ?= -std=c99 -Wall -Wextra -DPLATFORM_MEMORY -DGRAPHICS_API_OPENGL_SOFTWARE \
              -I$(RAYLIB_SRC) -I.
LDLIBS     ?= $(RAYLIB_SRC)/libraylib.a -lm -lpthread

TEST_BINS  := test_framereplay test_wallclock test_backend_contract export_frames

.PHONY: all test clean viewer-syntax

all: $(TEST_BINS)

# Each test/tool links the reusable module plus the static raylib library.
test_framereplay: test_framereplay.c framereplay.c framereplay.h
	$(CC) $(CFLAGS) test_framereplay.c framereplay.c $(LDLIBS) -o $@

test_wallclock: test_wallclock.c framereplay.c framereplay.h
	$(CC) $(CFLAGS) test_wallclock.c framereplay.c $(LDLIBS) -o $@

test_backend_contract: test_backend_contract.c framereplay.c framereplay.h
	$(CC) $(CFLAGS) test_backend_contract.c framereplay.c $(LDLIBS) -o $@

export_frames: export_frames.c framereplay.c framereplay.h
	$(CC) $(CFLAGS) export_frames.c framereplay.c $(LDLIBS) -o $@

# Build raylib for the memory platform once (already built in this workspace by default).
raylib:
	$(MAKE) -C $(RAYLIB_SRC) PLATFORM=PLATFORM_MEMORY

# Run the full headless suite; any failing assertion exits non-zero.
test: all
	@./test_framereplay > /tmp/t1.log 2>&1; echo "test_framereplay:        exit $$? ($$(grep -c PASS /tmp/t1.log) assertions)"
	@./test_wallclock    > /tmp/t2.log 2>&1; echo "test_wallclock:          exit $$? ($$(grep -c PASS /tmp/t2.log) assertions)"
	@./test_backend_contract > /tmp/t3.log 2>&1; echo "test_backend_contract:   exit $$? ($$(grep -c PASS /tmp/t3.log) assertions)"
	@./export_frames      > /tmp/t4.log 2>&1; echo "export_frames:           exit $$?"
	@echo "PNGs written: $$(ls frame*_memory*.png 2>/dev/null | wc -l)"

# The window viewer cannot link headless; verify it still compiles against the module API.
viewer-syntax:
	$(CC) -std=c99 -fsyntax-only -I$(RAYLIB_SRC) -I. replay_viewer.c

clean:
	rm -f $(TEST_BINS) *.o frame*_memory*.png viewer_frame*.png
