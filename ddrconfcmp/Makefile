# Makefile for ddrconfcmp

CC = gcc
CFLAGS = -Wall -Wextra -I../include -I.
TARGET = ddrconfcmp
SRC = ddrconfcmp.c

VERSION ?= v25.09
CONFIG_DIR = ../configs/$(VERSION)
OUTPUT_DIR = output

# Base configuration to compare against (2GB, 4GB, 8GB, or 16GB)
BASE ?= 2GB

# All available sizes
SIZES ?= 2GB 4GB 8GB 16GB

# Sizes to compare (all except BASE)
COMPARE_SIZES = $(filter-out $(BASE),$(SIZES))

all: run-checks

lpddr5_timing_left.c:
	@echo "Creating placeholder files..."
	@cp $(CONFIG_DIR)/DART-MX95_$(BASE)/lpddr5_timing.c lpddr5_timing_left.c

lpddr5_timing_right.c:
	@cp $(CONFIG_DIR)/DART-MX95_$(BASE)/lpddr5_timing.c lpddr5_timing_right.c

build-initial: lpddr5_timing_left.c lpddr5_timing_right.c
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

run-checks: build-initial
	@mkdir -p $(OUTPUT_DIR)
	@for size in $(COMPARE_SIZES); do \
		if [ ! -f "$(CONFIG_DIR)/DART-MX95_$$size/lpddr5_timing.c" ]; then \
			echo ""; \
			echo "═══════════════════════════════════════════════════════════════════════════"; \
			echo "Skipping $(BASE) vs $$size - configuration not found"; \
			echo "═══════════════════════════════════════════════════════════════════════════"; \
			continue; \
		fi; \
		echo ""; \
		echo "═══════════════════════════════════════════════════════════════════════════"; \
		echo "Comparing $(BASE) vs $$size"; \
		echo "═══════════════════════════════════════════════════════════════════════════"; \
		cp $(CONFIG_DIR)/DART-MX95_$(BASE)/lpddr5_timing.c lpddr5_timing_left.c; \
		cp $(CONFIG_DIR)/DART-MX95_$$size/lpddr5_timing.c lpddr5_timing_right.c; \
		$(CC) $(CFLAGS) -o $(TARGET) $(SRC); \
		output_file="$(OUTPUT_DIR)/$(VERSION)_$(BASE)_vs_$$size.txt"; \
		./$(TARGET) | tee $$output_file || true; \
		echo "Output saved to: $$output_file"; \
	done
	@rm -f lpddr5_timing_left.c lpddr5_timing_right.c
	@echo ""
	@echo "Cleaned up temporary files"

clean:
	rm -f $(TARGET) lpddr5_timing_left.c lpddr5_timing_right.c
	rm -rf $(OUTPUT_DIR)

.PHONY: all build-initial run-checks clean
