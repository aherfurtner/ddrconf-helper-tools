# Makefile for ddrconfdump

CC = gcc
CFLAGS = -Wall -Wextra -I../include -I.
TARGET = ddrconfdump
SRC = ddrconfdump.c

VERSION ?= v25.09
CONFIG_DIR = ../configs/$(VERSION)
OUTPUT_DIR = output

# All available sizes
ALL_SIZES ?= 2GB 4GB 8GB 16GB

all: run-dumps

run-dumps:
	@mkdir -p $(OUTPUT_DIR)
	@for size in $(ALL_SIZES); do \
		if [ ! -f "$(CONFIG_DIR)/DART-MX95_$$size/lpddr5_timing.c" ]; then \
			echo ""; \
			echo "═══════════════════════════════════════════════════════════════════════════"; \
			echo "Skipping $$size - configuration not found"; \
			echo "═══════════════════════════════════════════════════════════════════════════"; \
			continue; \
		fi; \
		echo ""; \
		echo "═══════════════════════════════════════════════════════════════════════════"; \
		echo "Dumping $(VERSION) DART-MX95_$$size"; \
		echo "═══════════════════════════════════════════════════════════════════════════"; \
		cp $(CONFIG_DIR)/DART-MX95_$$size/lpddr5_timing.c lpddr5_timing.c; \
		$(CC) $(CFLAGS) -o $(TARGET) $(SRC); \
		output_file="$(OUTPUT_DIR)/$(VERSION)_$$size.txt"; \
		./$(TARGET) > $$output_file; \
		echo "Output saved to: $$output_file"; \
	done
	@rm -f lpddr5_timing.c
	@echo ""
	@echo "Cleaned up temporary files"

clean:
	rm -f $(TARGET) lpddr5_timing.c
	rm -rf $(OUTPUT_DIR)

.PHONY: all run-dumps clean
