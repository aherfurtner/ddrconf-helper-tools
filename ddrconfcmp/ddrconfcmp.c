#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "confcmp.h"

/**
 * @file ddrconfcmp.c
 * @brief DDR Configuration Comparison Tool
 * 
 * This tool compares DDR memory configurations between different memory sizes
 * for the DART-MX95 platform. It performs deep structural and value comparison
 * of register configurations.
 * 
 * COMPARISON APPROACH:
 * ====================
 * 
 * 1. CONFIGURATION STRUCTURE:
 *    The tool compares multiple configuration structures:
 *    - ddrc_cfg: DDR controller base configuration
 *    - fsp_cfg: Frequency Set Point configurations (per FSP)
 *    - ddrphy_cfg: DDR PHY base configuration
 *    - fsp_msg: FSP message configurations (containing fsp_phy_cfg, fsp_phy_msgh_cfg, fsp_phy_pie_cfg)
 *    - ddrphy_trained_csr: Trained CSR values
 *    - ddrphy_pie: PHY PIE values
 * 
 * 2. COMPARISON PHASES:
 *    For each configuration array, the comparison follows these phases:
 * 
 *    a) DUPLICATE DETECTION (if DUPLICATE_CHECK=1):
 *       - Scans for duplicate register addresses within each configuration
 *       - Identifies duplicates with SAME values (warning - redundant entries)
 *       - Identifies duplicates with DIFFERENT values (error - conflicting configuration)
 *       - Groups all instances of each duplicate together in output
 * 
 *    b) STRUCTURAL COMPARISON:
 *       - Compares array lengths (num1 vs num2)
 *       - If lengths differ:
 *         * Identifies registers unique to LEFT configuration
 *         * Identifies registers unique to RIGHT configuration
 *         * Displays unique registers side-by-side for easy comparison
 *         * Creates temporary arrays containing only COMMON registers
 *         * Recursively compares common registers (see below)
 *       - Reports structural errors (different register sets)
 * 
 *    c) ORDER COMPARISON (same length only):
 *       - Checks if registers appear in identical order
 *       - If same order:
 *         * Direct value comparison at matching indices
 *       - If different order:
 *         * Searches for each register in opposite array
 *         * Compares values when register is found
 *         * Reports both value differences and order changes
 * 
 *    d) VALUE COMPARISON:
 *       - For each matching register pair, compares register values
 *       - Reports differences showing: old_value → new_value
 *       - Tracks total count of value differences
 * 
 * 3. RECURSIVE COMMON REGISTER COMPARISON:
 *    When array lengths differ but have common registers:
 *    - Allocates temporary arrays for common registers only
 *    - Maintains original order from LEFT configuration
 *    - Performs full comparison on common subset (phases a-d)
 *    - Displays results in nested visual boxes with extra indentation
 *    - Provides separate summary for common register comparison
 *    - Note: Parent comparison still returns -1 (structural error)
 * 
 * 4. RETURN VALUE SCHEME:
 *    Comparison functions use standardized return values:
 *    - -1: Structural error (different lengths or register sets)
 *    -  0: Identical registers, same order (diff_count shows value differences)
 *    -  1: Identical registers, different order (diff_count shows value differences)
 * 
 * 5. OUTPUT FORMATTING:
 *    - ANSI color codes: Red (errors), Yellow (warnings/info), Green (success)
 *    - Box drawing characters (┌─└┐┘│) for visual hierarchy
 *    - Nested indentation for sub-structures
 *    - Side-by-side display for unique registers (LEFT | RIGHT columns)
 *    - Summary messages after each comparison section
 * 
 * 6. CONFIGURATION OPTIONS:
 *    - DEBUG: Enable detailed debug output (default: 0)
 *    - DUPLICATE_CHECK: Enable duplicate register detection (default: 0)
 *      * When disabled: Functions not compiled, no performance impact
 *      * When enabled: Full duplicate detection with optimized grouped output
 * 
 * 7. MEMORY MANAGEMENT:
 *    - Dynamic allocation for temporary common register arrays
 *    - Proper malloc/free pairing for all allocations
 *    - Error handling for allocation failures
 *    - calloc used for zero-initialized processed flags in duplicate detection
 * 
 * EXAMPLE COMPARISON FLOW:
 * ========================
 * compare_ddrc_cfg_arrays(cfg1[37], cfg2[37]):
 *   1. Check duplicates in cfg1 → none found
 *   2. Check duplicates in cfg2 → none found
 *   3. Check lengths → 37 == 37 ✓
 *   4. Check order → registers in same order ✓
 *   5. Compare values → 4 differences found
 *   6. Return: 0 (same order), diff_count=4
 *   7. Print summary: "Registers match, 4 value differences"
 * 
 * compare_ddrphy_cfg_arrays(cfg1[411], cfg2[427]):
 *   1. Check duplicates → found in both ⚠
 *   2. Check lengths → 411 != 427 ✗
 *   3. Find unique registers:
 *      - LEFT: none
 *      - RIGHT: 16 unique registers at indices [17-44]
 *   4. Find common registers → 411 common
 *   5. Allocate temp arrays for 411 common registers
 *   6. Recursively compare common:
 *      - Check order → same order ✓
 *      - Compare values → 1 difference found
 *      - Return: 0, diff_count=1
 *      - Print nested summary: "Registers match, 1 value differences"
 *   7. Return: -1 (structural error)
 *   8. Print summary: "Structural differences found"
 */

#define DEBUG 1
#define SHOW_IDENTICAL_RANGES 0  /* Set to 1 to show identical position ranges, 0 to hide them */

/* Global flag for --list-duplicates option */
static int opt_list_duplicates = 0;

#if DEBUG
#define DEBUG_PRINT(...) do{ fprintf( stderr, __VA_ARGS__ ); } while( 0 )
#else
#define DEBUG_PRINT(...) do{ } while ( 0 )
#endif

/* ANSI color codes */
#define COLOR_RED     "\033[1;31m"
#define COLOR_GREEN   "\033[1;32m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_RESET   "\033[0m"

/* Format string constants for DDRC (32-bit registers and values) */
#define FMT_DDRC_REG        "0x%08x"
#define FMT_DDRC_VAL        "0x%08x"
#define FMT_DDRC_ENTRY      "[%3d] Reg " FMT_DDRC_REG " = " FMT_DDRC_VAL
#define FMT_DDRC_ENTRY_4    "[%4d] Reg " FMT_DDRC_REG " = " FMT_DDRC_VAL
#define FMT_DDRC_DIFF       "[%3d] Reg " FMT_DDRC_REG ": " FMT_DDRC_VAL " → " FMT_DDRC_VAL
#define FMT_DDRC_DIFF_4     "[%4d] Reg " FMT_DDRC_REG ": " FMT_DDRC_VAL " → " FMT_DDRC_VAL
#define DDRC_COLUMN_WIDTH   40

/* Format string constants for DDRPHY (20-bit registers, 16-bit values) */
#define FMT_PHY_REG         "0x%05x"
#define FMT_PHY_VAL         "0x%04x"
#define FMT_PHY_ENTRY       "[%3d] Reg " FMT_PHY_REG " = " FMT_PHY_VAL
#define FMT_PHY_ENTRY_4     "[%4d] Reg " FMT_PHY_REG " = " FMT_PHY_VAL
#define FMT_PHY_DIFF        "[%3d] Reg " FMT_PHY_REG ": " FMT_PHY_VAL " → " FMT_PHY_VAL
#define FMT_PHY_DIFF_4      "[%4d] Reg " FMT_PHY_REG ": " FMT_PHY_VAL " → " FMT_PHY_VAL
#define PHY_COLUMN_WIDTH    37

/**
 * @brief Calculate CRC32 checksum
 * @param addr Pointer to data buffer
 * @param size Size of data in bytes
 * @return CRC32 checksum
 */
static uint32_t compute_crc32(const uint8_t *addr, uint32_t size)
{
    const uint8_t *a = addr;
    uint32_t sz = size;
    uint32_t crc = 0U;

    /* Poly table */
    static uint32_t const s_crcTable[] =
    {
        0x4DBDF21CU, 0x500AE278U, 0x76D3D2D4U, 0x6B64C2B0U,
        0x3B61B38CU, 0x26D6A3E8U, 0x000F9344U, 0x1DB88320U,
        0xA005713CU, 0xBDB26158U, 0x9B6B51F4U, 0x86DC4190U,
        0xD6D930ACU, 0xCB6E20C8U, 0xEDB71064U, 0xF0000000U
    };

    /* Loop over data */
    while (sz > 0U)
    {
        crc = (crc >> 4U) ^ s_crcTable[(crc ^ (((uint32_t)(*a)) >> 0U))
                & 0x0FU];
        crc = (crc >> 4U) ^ s_crcTable[(crc ^ (((uint32_t) (*a)) >> 4U))
                & 0x0FU];
        a++;
        sz--;
    }

    /* Return CRC */
    return crc;
}

/**
 * @brief Print error message with red color
 */
static void print_error(const char *indent, const char *format, ...) {
	va_list args;
	printf("%s" COLOR_RED "E: ", indent);
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	printf(COLOR_RESET "\n");
}

/**
 * @brief Print warning message with yellow color
 */
static void print_warning(const char *indent, const char *format, ...) {
	va_list args;
	printf("%s" COLOR_YELLOW "W: ", indent);
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	printf(COLOR_RESET "\n");
}

/**
 * @brief Print info message with yellow color
 */
static void print_info(const char *indent, const char *format, ...) {
	va_list args;
	printf("%s" COLOR_YELLOW "I: ", indent);
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	printf(COLOR_RESET "\n");
}

/**
 * @brief Print success message with green color
 */
static void print_success(const char *indent, const char *format, ...) {
	va_list args;
	printf("%s" COLOR_GREEN, indent);
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	printf(COLOR_RESET "\n");
}

/**
 * @brief Print a side-by-side line with proper column alignment
 * 
 * @param left Left column content (can be empty string for right-only)
 * @param right Right column content (can be empty string for left-only)
 * @param indent Indentation string
 * @param column_width Width of each column
 */
static void print_side_by_side(const char *left, const char *right, 
                               const char *indent, int column_width) {
	printf("%s  %-*s  %s\n", indent, column_width, left, right);
}

/**
 * @brief Print unique register display header
 * 
 * @param indent Indentation string
 * @param column_width Width of each column
 */
static void print_unique_header(const char *indent, int column_width) {
	print_info(indent, "Unique registers:");
	printf("%s  %-*s  %s\n", indent, column_width, "LEFT", "RIGHT");
	
	/* Print separator line matching column width */
	const char *sep_char = "─";
	printf("%s  ", indent);
	for (int i = 0; i < column_width; i++) {
		printf("%s", sep_char);
	}
	printf("  ");
	for (int i = 0; i < column_width; i++) {
		printf("%s", sep_char);
	}
	printf("\n");
}

/**
 * @brief Print reordered register display header
 * 
 * @param indent Indentation string
 */
static void print_reorder_header(const char *indent) {
	print_info(indent, "Reordered registers:");
	printf("%s  LEFT                                 RIGHT\n", indent);
	printf("%s  ───────────────────────────────────  ───────────────────────────────────\n", indent);
}

/* ============================================================================
 * DDRC-specific helper functions
 * ============================================================================ */

/**
 * @brief Find unique registers and display them side-by-side for DDRC
 */
static void find_and_display_unique_ddrc(const struct ddrc_cfg_param *cfg1, unsigned int num1,
                                          const struct ddrc_cfg_param *cfg2, unsigned int num2,
                                          const char *indent) {
	int left_count = 0, right_count = 0;
	
	/* Count unique registers */
	for (int i = 0; i < (int)num1; i++) {
		int found = 0;
		for (int j = 0; j < (int)num2; j++) {
			if (cfg1[i].reg == cfg2[j].reg) {
				found = 1;
				break;
			}
		}
		if (!found) left_count++;
	}
	
	for (int i = 0; i < (int)num2; i++) {
		int found = 0;
		for (int j = 0; j < (int)num1; j++) {
			if (cfg2[i].reg == cfg1[j].reg) {
				found = 1;
				break;
			}
		}
		if (!found) right_count++;
	}
	
	if (left_count == 0 && right_count == 0) {
		return;
	}
	
	print_unique_header(indent, DDRC_COLUMN_WIDTH);
	
	int max_unique = (left_count > right_count) ? left_count : right_count;
	
	for (int line = 0; line < max_unique; line++) {
		char left_str[DDRC_COLUMN_WIDTH + 1] = "";
		char right_str[DDRC_COLUMN_WIDTH + 1] = "";
		
		/* Find next unique in left */
		if (num1 > num2) {
			int found_count = 0;
			for (int i = 0; i < (int)num1; i++) {
				int found = 0;
				for (int j = 0; j < (int)num2; j++) {
					if (cfg1[i].reg == cfg2[j].reg) {
						found = 1;
						break;
					}
				}
				if (!found) {
					if (found_count == line) {
						snprintf(left_str, sizeof(left_str), FMT_DDRC_ENTRY, i, cfg1[i].reg, cfg1[i].val);
						break;
					}
					found_count++;
				}
			}
		}
		
		/* Find next unique in right */
		if (num2 > num1) {
			int found_count = 0;
			for (int i = 0; i < (int)num2; i++) {
				int found = 0;
				for (int j = 0; j < (int)num1; j++) {
					if (cfg2[i].reg == cfg1[j].reg) {
						found = 1;
						break;
					}
				}
				if (!found) {
					if (found_count == line) {
						snprintf(right_str, sizeof(right_str), FMT_PHY_ENTRY, i, cfg2[i].reg, cfg2[i].val);
						break;
					}
					found_count++;
				}
			}
		}
		
		print_side_by_side(left_str, right_str, indent, DDRC_COLUMN_WIDTH);
	}
}

/**
 * @brief Count common registers for DDRC
 */
static int count_common_ddrc(const struct ddrc_cfg_param *cfg1, unsigned int num1,
                              const struct ddrc_cfg_param *cfg2, unsigned int num2,
                              unsigned int *common1, unsigned int *common2) {
	*common1 = 0;
	*common2 = 0;
	
	for (int i = 0; i < (int)num1; i++) {
		for (int j = 0; j < (int)num2; j++) {
			if (cfg1[i].reg == cfg2[j].reg) {
				(*common1)++;
				break;
			}
		}
	}
	
	for (int i = 0; i < (int)num2; i++) {
		for (int j = 0; j < (int)num1; j++) {
			if (cfg2[i].reg == cfg1[j].reg) {
				(*common2)++;
				break;
			}
		}
	}
	
	return (*common1 == *common2) ? 0 : -1;
}

/**
 * @brief Extract common registers into new arrays for DDRC
 */
static void extract_common_ddrc(const struct ddrc_cfg_param *cfg1, unsigned int num1,
                                 const struct ddrc_cfg_param *cfg2, unsigned int num2,
                                 struct ddrc_cfg_param *common1, struct ddrc_cfg_param *common2) {
	unsigned int idx1 = 0, idx2 = 0;
	
	for (int i = 0; i < (int)num1; i++) {
		for (int j = 0; j < (int)num2; j++) {
			if (cfg1[i].reg == cfg2[j].reg) {
				common1[idx1++] = cfg1[i];
				break;
			}
		}
	}
	
	for (int i = 0; i < (int)num2; i++) {
		for (int j = 0; j < (int)num1; j++) {
			if (cfg2[i].reg == cfg1[j].reg) {
				common2[idx2++] = cfg2[i];
				break;
			}
		}
	}
}

/* ============================================================================
 * DDRPHY-specific helper functions
 * ============================================================================ */

/**
 * @brief Find unique registers and display them side-by-side for DDRPHY
 */
static void find_and_display_unique_ddrphy(const struct ddrphy_cfg_param *cfg1, unsigned int num1,
                                            const struct ddrphy_cfg_param *cfg2, unsigned int num2,
                                            const char *indent) {
	int left_count = 0, right_count = 0;
	
	/* Count unique registers */
	for (int i = 0; i < (int)num1; i++) {
		int found = 0;
		for (int j = 0; j < (int)num2; j++) {
			if (cfg1[i].reg == cfg2[j].reg) {
				found = 1;
				break;
			}
		}
		if (!found) left_count++;
	}
	
	for (int i = 0; i < (int)num2; i++) {
		int found = 0;
		for (int j = 0; j < (int)num1; j++) {
			if (cfg2[i].reg == cfg1[j].reg) {
				found = 1;
				break;
			}
		}
		if (!found) right_count++;
	}
	
	if (left_count == 0 && right_count == 0) {
		return;
	}
	
	print_unique_header(indent, PHY_COLUMN_WIDTH);
	
	int max_unique = (left_count > right_count) ? left_count : right_count;
	
	for (int line = 0; line < max_unique; line++) {
		char left_str[PHY_COLUMN_WIDTH + 1] = "";
		char right_str[PHY_COLUMN_WIDTH + 1] = "";
		
		/* Find next unique in left */
		if (num1 > num2) {
			int found_count = 0;
			for (int i = 0; i < (int)num1; i++) {
				int found = 0;
				for (int j = 0; j < (int)num2; j++) {
					if (cfg1[i].reg == cfg2[j].reg) {
						found = 1;
						break;
					}
				}
				if (!found) {
					if (found_count == line) {
						snprintf(left_str, sizeof(left_str), FMT_PHY_ENTRY, i, cfg1[i].reg, cfg1[i].val);
						break;
					}
					found_count++;
				}
			}
		}
		
		/* Find next unique in right */
		if (num2 > num1) {
			int found_count = 0;
			for (int i = 0; i < (int)num2; i++) {
				int found = 0;
				for (int j = 0; j < (int)num1; j++) {
					if (cfg2[i].reg == cfg1[j].reg) {
						found = 1;
						break;
					}
				}
				if (!found) {
					if (found_count == line) {
						snprintf(right_str, sizeof(right_str), FMT_PHY_ENTRY, i, cfg2[i].reg, cfg2[i].val);
						break;
					}
					found_count++;
				}
			}
		}
		
		print_side_by_side(left_str, right_str, indent, PHY_COLUMN_WIDTH);
	}
}

/**
 * @brief Count common registers for DDRPHY
 */
static int count_common_ddrphy(const struct ddrphy_cfg_param *cfg1, unsigned int num1,
                                const struct ddrphy_cfg_param *cfg2, unsigned int num2,
                                unsigned int *common1, unsigned int *common2) {
	*common1 = 0;
	*common2 = 0;
	
	for (int i = 0; i < (int)num1; i++) {
		for (int j = 0; j < (int)num2; j++) {
			if (cfg1[i].reg == cfg2[j].reg) {
				(*common1)++;
				break;
			}
		}
	}
	
	for (int i = 0; i < (int)num2; i++) {
		for (int j = 0; j < (int)num1; j++) {
			if (cfg2[i].reg == cfg1[j].reg) {
				(*common2)++;
				break;
			}
		}
	}
	
	return (*common1 == *common2) ? 0 : -1;
}

/**
 * @brief Extract common registers into new arrays for DDRPHY
 */
static void extract_common_ddrphy(const struct ddrphy_cfg_param *cfg1, unsigned int num1,
                                   const struct ddrphy_cfg_param *cfg2, unsigned int num2,
                                   struct ddrphy_cfg_param *common1, struct ddrphy_cfg_param *common2) {
	unsigned int idx1 = 0, idx2 = 0;
	
	for (int i = 0; i < (int)num1; i++) {
		for (int j = 0; j < (int)num2; j++) {
			if (cfg1[i].reg == cfg2[j].reg) {
				common1[idx1++] = cfg1[i];
				break;
			}
		}
	}
	
	for (int i = 0; i < (int)num2; i++) {
		for (int j = 0; j < (int)num1; j++) {
			if (cfg2[i].reg == cfg1[j].reg) {
				common2[idx2++] = cfg2[i];
				break;
			}
		}
	}
}

/**
 * @brief Print consolidated summary based on comparison return value
 * 
 * This function interprets the standardized return values from comparison functions
 * and prints appropriate summary messages.
 * 
 * Return value scheme:
 *   -1: Different size or different register sets (structural error)
 *    0: Identical registers, same order (diff_count indicates value differences)
 *    1: Identical registers, different order (diff_count indicates value differences)
 * 
 * @param result Return value from comparison function (-1, 0, 1)
 * @param diff_count Number of value differences
 * @param indent Indentation string for formatting output
 */
static void print_comparison_summary(int result, int diff_count, const char *indent) {
	switch (result) {
		case -1:
			/* Different size or register sets - error already printed */
			/* Structural differences message now printed earlier, at the point of detection */
			break;
		case 0:
			/* Same order */
			if (diff_count == 0) {
				print_success(indent, "Registers and values match");
			}
			/* Warning for value differences already printed inside comparison function */
			break;
		case 1:
			/* Different order - warning already printed inside comparison function */
			break;
		default:
			print_error(indent, "Unexpected comparison result %d", result);
			break;
	}
}

/* Structure to hold duplicate register information */
struct duplicate_info {
	unsigned int reg;
	unsigned int indices[64];  /* Max 64 duplicate occurrences */
	unsigned int values[64];
	unsigned int count;
};

/**
 * @brief Find duplicate registers in ddrc_cfg_param array
 * 
 * @param cfg Configuration array to check
 * @param num Number of entries in array
 * @param dups Output array to store duplicate info
 * @param max_dups Maximum number of duplicates to store
 * @return Number of duplicate register addresses found
 */
static int find_duplicates_ddrc(const struct ddrc_cfg_param *cfg, unsigned int num,
                                 struct duplicate_info *dups, int max_dups) {
	int dup_count = 0;
	int *processed = calloc(num, sizeof(int));
	
	if (!processed) {
		return 0;
	}
	
	for (unsigned int i = 0; i < num && dup_count < max_dups; i++) {
		if (processed[i]) continue;
		
		dups[dup_count].reg = cfg[i].reg;
		dups[dup_count].indices[0] = i;
		dups[dup_count].values[0] = cfg[i].val;
		dups[dup_count].count = 1;
		
		for (unsigned int j = i + 1; j < num; j++) {
			if (cfg[i].reg == cfg[j].reg && dups[dup_count].count < 64) {
				dups[dup_count].indices[dups[dup_count].count] = j;
				dups[dup_count].values[dups[dup_count].count] = cfg[j].val;
				dups[dup_count].count++;
				processed[j] = 1;
			}
		}
		
		if (dups[dup_count].count > 1) {
			dup_count++;
		}
	}
	
	free(processed);
	return dup_count;
}

/**
 * @brief Find duplicate registers in ddrphy_cfg_param array
 * 
 * @param cfg Configuration array to check
 * @param num Number of entries in array
 * @param dups Output array to store duplicate info
 * @param max_dups Maximum number of duplicates to store
 * @return Number of duplicate register addresses found
 */
static int find_duplicates_ddrphy(const struct ddrphy_cfg_param *cfg, unsigned int num,
                                   struct duplicate_info *dups, int max_dups) {
	int dup_count = 0;
	int *processed = calloc(num, sizeof(int));
	
	if (!processed) {
		return 0;
	}
	
	for (unsigned int i = 0; i < num && dup_count < max_dups; i++) {
		if (processed[i]) continue;
		
		dups[dup_count].reg = cfg[i].reg;
		dups[dup_count].indices[0] = i;
		dups[dup_count].values[0] = cfg[i].val;
		dups[dup_count].count = 1;
		
		for (unsigned int j = i + 1; j < num; j++) {
			if (cfg[i].reg == cfg[j].reg && dups[dup_count].count < 64) {
				dups[dup_count].indices[dups[dup_count].count] = j;
				dups[dup_count].values[dups[dup_count].count] = cfg[j].val;
				dups[dup_count].count++;
				processed[j] = 1;
			}
		}
		
		if (dups[dup_count].count > 1) {
			dup_count++;
		}
	}
	
	free(processed);
	return dup_count;
}

/**
 * @brief Print duplicate registers side-by-side for DDRC
 */
static void print_duplicates_ddrc_sidebyside(const struct duplicate_info *left_dups, int left_count,
                                              const struct duplicate_info *right_dups, int right_count,
                                              const char *indent) {
	if (left_count == 0 && right_count == 0) {
		return;
	}
	
	print_info(indent, "Duplicate registers:");
	printf("%s  LEFT                                   RIGHT\n", indent);
	printf("%s  ─────────────────────────────────────  ─────────────────────────────────────\n", indent);
	
	int max_count = left_count > right_count ? left_count : right_count;
	
	for (int i = 0; i < max_count; i++) {
		char left_buf[50] = "";
		char right_buf[50] = "";
		
		if (i < left_count) {
			snprintf(left_buf, sizeof(left_buf), "0x%08x (%u times)",
			         left_dups[i].reg, left_dups[i].count);
		}
		
		if (i < right_count) {
			snprintf(right_buf, sizeof(right_buf), "0x%08x (%u times)",
			         right_dups[i].reg, right_dups[i].count);
		}
		
		printf("%s  %-37s  %-37s\n", indent, left_buf, right_buf);
	}
}

/**
 * @brief Print duplicate registers side-by-side for DDRPHY
 */
static void print_duplicates_ddrphy_sidebyside(const struct duplicate_info *left_dups, int left_count,
                                                const struct duplicate_info *right_dups, int right_count,
                                                const char *indent) {
	if (left_count == 0 && right_count == 0) {
		return;
	}
	
	print_info(indent, "Duplicate registers:");
	printf("%s  LEFT                                   RIGHT\n", indent);
	printf("%s  ─────────────────────────────────────  ─────────────────────────────────────\n", indent);
	
	int max_count = left_count > right_count ? left_count : right_count;
	
	for (int i = 0; i < max_count; i++) {
		char left_buf[50] = "";
		char right_buf[50] = "";
		
		if (i < left_count) {
			snprintf(left_buf, sizeof(left_buf), "0x%05x (%u times)",
			         left_dups[i].reg, left_dups[i].count);
		}
		
		if (i < right_count) {
			snprintf(right_buf, sizeof(right_buf), "0x%05x (%u times)",
			         right_dups[i].reg, right_dups[i].count);
		}
		
		printf("%s  %-37s  %-37s\n", indent, left_buf, right_buf);
	}
}

/**
 * @brief Check if duplicates interfere with value differences and warn about them
 * 
 * @param cfg1 Left configuration array
 * @param cfg2 Right configuration array
 * @param num Number of entries (must be same for both)
 * @param left_dups Left duplicate info array
 * @param left_dup_count Number of left duplicates
 * @param right_dups Right duplicate info array
 * @param right_dup_count Number of right duplicates
 * @param indent Indentation for output
 * @param is_ddrc True for DDRC (32-bit regs), false for DDRPHY (20-bit regs)
 */
static void check_duplicate_interference(const void *cfg1, const void *cfg2, unsigned int num,
                                         const struct duplicate_info *left_dups, int left_dup_count,
                                         const struct duplicate_info *right_dups, int right_dup_count,
                                         const char *indent, int is_ddrc) {
	int interference_found = 0;
	unsigned int reported_regs[100];  /* Track which registers we've already reported */
	int reported_count = 0;
	
	/* Check both left and right duplicates, but report each register only once */
	for (int side = 0; side < 2; side++) {
		const struct duplicate_info *dups = (side == 0) ? left_dups : right_dups;
		int dup_count = (side == 0) ? left_dup_count : right_dup_count;
		
		for (int d = 0; d < dup_count; d++) {
			unsigned int dup_reg = dups[d].reg;
			
			/* Check if we already reported this register */
			int already_reported = 0;
			for (int r = 0; r < reported_count; r++) {
				if (reported_regs[r] == dup_reg) {
					already_reported = 1;
					break;
				}
			}
			if (already_reported) continue;
			
			/* Check if this duplicate register has value differences */
			for (unsigned int i = 0; i < num; i++) {
				unsigned int reg_i, val1, val2;
				
				if (is_ddrc) {
					const struct ddrc_cfg_param *c1 = (const struct ddrc_cfg_param *)cfg1;
					const struct ddrc_cfg_param *c2 = (const struct ddrc_cfg_param *)cfg2;
					reg_i = c1[i].reg;
					val1 = c1[i].val;
					val2 = c2[i].val;
				} else {
					const struct ddrphy_cfg_param *c1 = (const struct ddrphy_cfg_param *)cfg1;
					const struct ddrphy_cfg_param *c2 = (const struct ddrphy_cfg_param *)cfg2;
					reg_i = c1[i].reg;
					val1 = c1[i].val;
					val2 = c2[i].val;
				}
				
				if (reg_i == dup_reg && val1 != val2) {
					if (!interference_found) {
						print_warning(indent, "Duplicate registers involved in value differences:");
						interference_found = 1;
					}
					
					/* Print the duplicate register with all its instances */
					if (is_ddrc) {
						printf("%s    Reg 0x%08x: duplicated %u times at indices:", indent, dup_reg, dups[d].count);
					} else {
						printf("%s    Reg 0x%05x: duplicated %u times at indices:", indent, dup_reg, dups[d].count);
					}
					for (unsigned int idx = 0; idx < dups[d].count; idx++) {
						printf(" [%u]", dups[d].indices[idx]);
					}
					printf("\n");
					
					/* Show the values at each duplicate location */
					if (is_ddrc) {
						const struct ddrc_cfg_param *c1 = (const struct ddrc_cfg_param *)cfg1;
						const struct ddrc_cfg_param *c2 = (const struct ddrc_cfg_param *)cfg2;
						for (unsigned int idx = 0; idx < dups[d].count; idx++) {
							unsigned int pos = dups[d].indices[idx];
							printf("%s        [%u] Left=0x%08x, Right=0x%08x\n", 
							       indent, pos, c1[pos].val, c2[pos].val);
						}
					} else {
						const struct ddrphy_cfg_param *c1 = (const struct ddrphy_cfg_param *)cfg1;
						const struct ddrphy_cfg_param *c2 = (const struct ddrphy_cfg_param *)cfg2;
						for (unsigned int idx = 0; idx < dups[d].count; idx++) {
							unsigned int pos = dups[d].indices[idx];
							printf("%s        [%u] Left=0x%04x, Right=0x%04x\n", 
							       indent, pos, c1[pos].val, c2[pos].val);
						}
					}
					
					/* Mark this register as reported */
					if (reported_count < 100) {
						reported_regs[reported_count++] = dup_reg;
					}
					break;  /* Move to next duplicate */
				}
			}
		}
	}
}

/**
 * @brief Compare two ddrc_cfg_param arrays
 * 
 * @param cfg1 First configuration array
 * @param num1 Number of entries in first array
 * @param cfg2 Second configuration array
 * @param num2 Number of entries in second array
 * @param indent Indentation string for output
 * @param diff_count_p Pointer to store the number of value differences (optional, can be NULL)
 * @param print_header If non-zero, print entry count and size information
 * @return int Result code: -1 (structural error), 0 (same order), 1 (different order)
 */
static int compare_ddrc_cfg_arrays(const struct ddrc_cfg_param *cfg1, unsigned int num1,
                                     const struct ddrc_cfg_param *cfg2, unsigned int num2,
                                     const char *indent, int *diff_count_p, int print_header) {
	int i;
	int diff_count = 0;
	int has_error = 0;
	int same_order = 1;
	
	if (print_header) {
		uint32_t crc_left = compute_crc32((const uint8_t *)cfg1, num1 * sizeof(struct ddrc_cfg_param));
		uint32_t crc_right = compute_crc32((const uint8_t *)cfg2, num2 * sizeof(struct ddrc_cfg_param));
		printf("%sEntries: Left=%u, Right=%u\n", indent, num1, num2);
		printf("%sSize:    Left=%u bytes (%.2f kB), Right=%u bytes (%.2f kB)\n",
		       indent, num1 * 8, num1 * 8 / 1024.0, num2 * 8, num2 * 8 / 1024.0);
		printf("%sCRC:     Left=0x%08x, Right=0x%08x\n", indent, crc_left, crc_right);
	}
	
	if (num1 != num2) {
		print_warning(indent, "Structural differences found");
		has_error = 1;
		
		/* Find and display unique registers side-by-side */
		find_and_display_unique_ddrc(cfg1, num1, cfg2, num2, indent);
		
		/* Compare common registers */
		printf("\n");
		printf("%s┌─ Comparing common registers ──────────────────────────────┐\n", indent);
		
		/* Count common registers */
		unsigned int common_count1, common_count2;
		if (count_common_ddrc(cfg1, num1, cfg2, num2, &common_count1, &common_count2) != 0) {
			print_error(indent, "Internal error: common register counts don't match (%u vs %u)", common_count1, common_count2);
			printf("%s└──────────────────────────────────────────────────────────┘\n", indent);
			return -1;
		}
		
		if (common_count1 > 0) {
			/* Allocate temporary arrays for common registers */
			struct ddrc_cfg_param *common1 = malloc(common_count1 * sizeof(struct ddrc_cfg_param));
			struct ddrc_cfg_param *common2 = malloc(common_count2 * sizeof(struct ddrc_cfg_param));
			
			if (common1 && common2) {
				/* Extract common registers */
				extract_common_ddrc(cfg1, num1, cfg2, num2, common1, common2);
				
				char nested_indent[32];
				snprintf(nested_indent, sizeof(nested_indent), "%s  ", indent);
				
				/* Recursively compare common registers */
				int common_result = compare_ddrc_cfg_arrays(common1, common_count1, 
				                                            common2, common_count2,
				                                            nested_indent, diff_count_p, 1);
				
				/* Print summary for common register comparison */
				int common_diff_count = diff_count_p ? *diff_count_p : 0;
				print_comparison_summary(common_result, common_diff_count, nested_indent);
				printf("%s└──────────────────────────────────────────────────────────┘\n", indent);
				
				free(common1);
				free(common2);
				
				/* Still return -1 due to structural difference, but we showed common register comparison */
				return -1;
			} else {
				print_error(indent, "Memory allocation failed for common register comparison");
				if (common1) free(common1);
				if (common2) free(common2);
			}
		} else {
			print_info(indent, "No common registers found");
		}
		
		return -1; /* Length mismatch is structural error */
	}
	
	/* Same length - check if registers are in same order */
	for (i = 0; i < (int)num1; i++) {
		if (cfg1[i].reg != cfg2[i].reg) {
			same_order = 0;
			break;
		}
	}
	
	if (same_order) {
		/* Same order - count value differences first */
		for (i = 0; i < (int)num1; i++) {
			if (cfg1[i].val != cfg2[i].val) {
				diff_count++;
			}
		}
		
		/* Print summary before details */
		if (diff_count > 0) {
			print_info(indent, "Registers match, %d value differences", diff_count);
			print_info(indent, "Register value differences:");
		}
		
		/* Now print the details */
		for (i = 0; i < (int)num1; i++) {
			if (cfg1[i].val != cfg2[i].val) {
				printf("%s    " FMT_DDRC_DIFF "\n", 
				       indent, i, cfg1[i].reg, cfg1[i].val, cfg2[i].val);
			}
		}
		
		/* Return: 0 if all match, 1 if value differences */
		if (diff_count_p) *diff_count_p = diff_count;
		return 0;
	} else {
		/* Different order - check if all registers exist in both arrays */
		int all_present = 1;
		
		/* Check if all registers from left exist in right */
		for (i = 0; i < (int)num1; i++) {
			int j, found = 0;
			for (j = 0; j < (int)num2; j++) {
				if (cfg1[i].reg == cfg2[j].reg) {
					found = 1;
					break;
				}
			}
			if (!found) {
				if (all_present) {
					print_error(indent, "Arrays have same length but different register sets!");
					print_info(indent, "Registers in LEFT but not in RIGHT:");
					all_present = 0;
				}
				printf("%s    [%3d] Reg 0x%08x = 0x%08x\n", indent, i, cfg1[i].reg, cfg1[i].val);
			}
		}
		
		/* Check if all registers from right exist in left */
		for (i = 0; i < (int)num2; i++) {
			int j, found = 0;
			for (j = 0; j < (int)num1; j++) {
				if (cfg2[i].reg == cfg1[j].reg) {
					found = 1;
					break;
				}
			}
			if (!found) {
				if (all_present) {
					print_error(indent, "Arrays have same length but different register sets!");
					all_present = 0;
				}
				if (!all_present && i == 0) {
					print_info(indent, "Registers in RIGHT but not in LEFT:");
				}
				printf("%s    " FMT_DDRC_ENTRY "\n", indent, i, cfg2[i].reg, cfg2[i].val);
			}
		}
		
		if (!all_present) {
			/* Different register sets - structural error */
			return -1;
		}
		
		/* All registers present but different order - print warning first, then analyze with LCS-based diff */
		print_warning(indent, "Registers match, different order");
		print_reorder_header(indent);
		
		/* Use LCS (Longest Common Subsequence) approach to find matching blocks */
		int i1 = 0, i2 = 0;
		
		while (i1 < (int)num1 && i2 < (int)num2) {
			/* Check if registers at current positions match */
			if (cfg1[i1].reg == cfg2[i2].reg) {
				/* Found a matching block - scan forward to find the extent */
				int start_i1 = i1;
				int start_i2 = i2;
				
				while (i1 < (int)num1 && i2 < (int)num2 && cfg1[i1].reg == cfg2[i2].reg) {
					i1++;
					i2++;
				}
				
#if SHOW_IDENTICAL_RANGES
				/* Only show if more than a few registers to reduce noise */
				if (i1 - start_i1 > 10) {
					printf("%s  [%4d-%4d] (%d registers)           [%4d-%4d] (%d registers)\n",
					       indent, start_i1, i1 - 1, i1 - start_i1, start_i2, i2 - 1, i2 - start_i2);
				}
#else
				(void)start_i1; (void)start_i2; /* Unused when SHOW_IDENTICAL_RANGES is 0 */
#endif
			} else {
				/* Registers don't match - find where blocks appear */
				int block_start_i1 = i1;
				
				/* Scan forward in left to find extent of mismatched block */
				while (i1 < (int)num1) {
					/* Check if this register from left appears soon in right */
					int found_soon = 0;
					for (int check_j = i2; check_j < i2 + 50 && check_j < (int)num2; check_j++) {
						if (cfg1[i1].reg == cfg2[check_j].reg) {
							found_soon = 1;
							break;
						}
					}
					if (found_soon) break;
					i1++;
				}
				
				int block_start_i2 = i2;
				
				/* Scan forward in right to find extent of mismatched block */
				while (i2 < (int)num2) {
					/* Check if this register from right appears soon in left */
					int found_soon = 0;
					for (int check_i = i1; check_i < i1 + 50 && check_i < (int)num1; check_i++) {
						if (cfg2[i2].reg == cfg1[check_i].reg) {
							found_soon = 1;
							break;
						}
					}
					if (found_soon) break;
					i2++;
				}
				
				/* Display the relocated blocks side-by-side */
				if (block_start_i1 < i1 && block_start_i2 < i2) {
					/* Both sides have blocks - they're relocated */
					int left_count = i1 - block_start_i1;
					int right_count = i2 - block_start_i2;
					int max_show = (left_count < right_count) ? right_count : left_count;
					if (max_show > 10) max_show = 10;
					
					for (int k = 0; k < max_show; k++) {
						char left_buf[DDRC_COLUMN_WIDTH] = "";
						char right_buf[DDRC_COLUMN_WIDTH] = "";
						
						if (k < left_count) {
							snprintf(left_buf, sizeof(left_buf), FMT_DDRC_ENTRY_4,
							         block_start_i1 + k, cfg1[block_start_i1 + k].reg, cfg1[block_start_i1 + k].val);
						}
						if (k < right_count) {
							snprintf(right_buf, sizeof(right_buf), FMT_DDRC_ENTRY_4,
							         block_start_i2 + k, cfg2[block_start_i2 + k].reg, cfg2[block_start_i2 + k].val);
						}
						
						print_side_by_side(left_buf, right_buf, indent, 37);
					}
					
					if (left_count > 10 || right_count > 10) {
						char left_more[DDRC_COLUMN_WIDTH] = "";
						char right_more[DDRC_COLUMN_WIDTH] = "";
						if (left_count > 10) {
							snprintf(left_more, sizeof(left_more), "... (%d more)", left_count - 10);
						}
						if (right_count > 10) {
							snprintf(right_more, sizeof(right_more), "... (%d more)", right_count - 10);
						}
						print_side_by_side(left_more, right_more, indent, 37);
					}
				} else if (block_start_i1 < i1) {
					/* Only left has block */
					int left_count = i1 - block_start_i1;
					int show_count = (left_count < 10) ? left_count : 10;
					
					for (int k = 0; k < show_count; k++) {
						printf("%s  " FMT_DDRC_ENTRY_4 "\n",
						       indent, block_start_i1 + k, cfg1[block_start_i1 + k].reg, cfg1[block_start_i1 + k].val);
					}
					if (left_count > 10) {
						printf("%s  ... (%d more)\n", indent, left_count - 10);
					}
				} else if (block_start_i2 < i2) {
					/* Only right has block */
					int right_count = i2 - block_start_i2;
					int show_count = (right_count < 10) ? right_count : 10;
					
					for (int k = 0; k < show_count; k++) {
						char right_buf[DDRC_COLUMN_WIDTH];
						snprintf(right_buf, sizeof(right_buf), FMT_DDRC_ENTRY_4,
						         block_start_i2 + k, cfg2[block_start_i2 + k].reg, cfg2[block_start_i2 + k].val);
						print_side_by_side("", right_buf, indent, 37);
					}
					if (right_count > 10) {
						char more_buf[DDRC_COLUMN_WIDTH];
						snprintf(more_buf, sizeof(more_buf), "... (%d more)", right_count - 10);
						print_side_by_side("", more_buf, indent, 37);
					}
				}
			}
		}
		
		/* Handle remaining registers at the end */
		if (i1 < (int)num1) {
			int remain_count = (int)num1 - i1;
			int show_count = (remain_count < 10) ? remain_count : 10;
			for (int k = 0; k < show_count; k++) {
				printf("%s  " FMT_DDRC_ENTRY_4 "\n",
				       indent, i1 + k, cfg1[i1 + k].reg, cfg1[i1 + k].val);
			}
			if (remain_count > 10) {
				printf("%s  ... (%d more)\n", indent, remain_count - 10);
			}
		}
		if (i2 < (int)num2) {
			int remain_count = (int)num2 - i2;
			int show_count = (remain_count < 10) ? remain_count : 10;
			for (int k = 0; k < show_count; k++) {
				char right_buf[DDRC_COLUMN_WIDTH];
				snprintf(right_buf, sizeof(right_buf), FMT_DDRC_ENTRY_4,
				         i2 + k, cfg2[i2 + k].reg, cfg2[i2 + k].val);
				print_side_by_side("", right_buf, indent, DDRC_COLUMN_WIDTH - 3);
			}
			if (remain_count > 10) {
				print_side_by_side("", "...", indent, DDRC_COLUMN_WIDTH - 3);
				printf(" (%d more)\n", remain_count - 10);
			}
		}
		
		/* Count value differences first */
		for (i = 0; i < (int)num1; i++) {
			/* Find matching register in cfg2 */
			int j;
			for (j = 0; j < (int)num2; j++) {
				if (cfg1[i].reg == cfg2[j].reg) {
					if (cfg1[i].val != cfg2[j].val) {
						diff_count++;
					}
					break;
				}
			}
		}
		
		/* Print summary and details for value differences */
		if (diff_count > 0) {
			print_info(indent, "Value differences: %d", diff_count);
			print_info(indent, "Register value differences:");
			/* Now print the details */
			for (i = 0; i < (int)num1; i++) {
				int j;
				for (j = 0; j < (int)num2; j++) {
					if (cfg1[i].reg == cfg2[j].reg) {
						if (cfg1[i].val != cfg2[j].val) {
							printf("%s    " FMT_DDRC_DIFF_4 "\n", 
							       indent, i, cfg1[i].reg, cfg1[i].val, cfg2[j].val);
						}
						break;
					}
				}
			}
		}
		
		/* Return: 2 for different order (diff_count tracks value differences) */
		if (diff_count_p) *diff_count_p = diff_count;
		
		return 1;
	}
}

/**
 * @brief Compare two ddrphy_cfg_param arrays
 * 
 * @param cfg1 First configuration array
 * @param num1 Number of entries in first array
 * @param cfg2 Second configuration array
 * @param num2 Number of entries in second array
 * @param indent Indentation string for output
 * @param diff_count_p Pointer to store the number of value differences (optional, can be NULL)
 * @param print_header If non-zero, print entry count and size information
 * @return int Result code: -1 (structural error), 0 (same order), 1 (different order)
 */
static int compare_ddrphy_cfg_arrays(const struct ddrphy_cfg_param *cfg1, unsigned int num1,
                                       const struct ddrphy_cfg_param *cfg2, unsigned int num2,
                                       const char *indent, int *diff_count_p, int print_header) {
	int i;
	int diff_count = 0;
	int has_error = 0;
	int same_order = 1;
	
	if (print_header) {
		uint32_t crc_left = compute_crc32((const uint8_t *)cfg1, num1 * sizeof(struct ddrphy_cfg_param));
		uint32_t crc_right = compute_crc32((const uint8_t *)cfg2, num2 * sizeof(struct ddrphy_cfg_param));
		printf("%sEntries: Left=%u, Right=%u\n", indent, num1, num2);
		printf("%sSize:    Left=%u bytes (%.2f kB), Right=%u bytes (%.2f kB)\n",
		       indent, num1 * 6, num1 * 6 / 1024.0, num2 * 6, num2 * 6 / 1024.0);
		printf("%sCRC:     Left=0x%08x, Right=0x%08x\n", indent, crc_left, crc_right);
	}
	
	if (num1 != num2) {
		print_warning(indent, "Structural differences found");
		has_error = 1;
		
		/* Find and display unique registers side-by-side */
		find_and_display_unique_ddrphy(cfg1, num1, cfg2, num2, indent);
		
		/* Compare common registers */
		printf("\n");
		printf("%s┌─ Comparing common registers ──────────────────────────────┐\n", indent);
		
		/* Count common registers */
		unsigned int common_count1, common_count2;
		if (count_common_ddrphy(cfg1, num1, cfg2, num2, &common_count1, &common_count2) != 0) {
			print_error(indent, "Internal error: common register counts don't match (%u vs %u)", common_count1, common_count2);
			printf("%s└──────────────────────────────────────────────────────────┘\n", indent);
			return -1;
		}
		
		if (common_count1 > 0) {
			/* Allocate temporary arrays for common registers */
			struct ddrphy_cfg_param *common1 = malloc(common_count1 * sizeof(struct ddrphy_cfg_param));
			struct ddrphy_cfg_param *common2 = malloc(common_count2 * sizeof(struct ddrphy_cfg_param));
			
			if (common1 && common2) {
				/* Extract common registers */
				extract_common_ddrphy(cfg1, num1, cfg2, num2, common1, common2);
				
				char nested_indent[32];
				snprintf(nested_indent, sizeof(nested_indent), "%s  ", indent);
				
				/* Recursively compare common registers */
				int common_result = compare_ddrphy_cfg_arrays(common1, common_count1, 
				                                              common2, common_count2,
				                                              nested_indent, diff_count_p, 1);
				
				/* Print summary for common register comparison */
				int common_diff_count = diff_count_p ? *diff_count_p : 0;
				print_comparison_summary(common_result, common_diff_count, nested_indent);
				printf("%s└──────────────────────────────────────────────────────────┘\n", indent);
				
				free(common1);
				free(common2);
				
				/* Still return -1 due to structural difference, but we showed common register comparison */
				return -1;
			} else {
				print_error(indent, "Memory allocation failed for common register comparison");
				if (common1) free(common1);
				if (common2) free(common2);
			}
		} else {
			print_info(indent, "No common registers found");
		}
		
		return -1; /* Length mismatch is structural error */
	}
	
	/* Same length - check if registers are in same order */
	for (i = 0; i < (int)num1; i++) {
		if (cfg1[i].reg != cfg2[i].reg) {
			same_order = 0;
			break;
		}
	}
	if (same_order) {
		/* Same order - count value differences first */
		for (i = 0; i < (int)num1; i++) {
			if (cfg1[i].val != cfg2[i].val) {
				diff_count++;
			}
		}
		
		/* Print summary before details */
		if (diff_count > 0) {
			print_info(indent, "Registers match, %d value differences", diff_count);
			print_info(indent, "Register value differences:");
		}
		
		/* Now print the details */
		for (i = 0; i < (int)num1; i++) {
			if (cfg1[i].val != cfg2[i].val) {
				printf("%s    " FMT_PHY_DIFF "\n", 
				       indent, i, cfg1[i].reg, cfg1[i].val, cfg2[i].val);
			}
		}
		
		/* Return: 0 if all match, 1 if value differences */
		if (diff_count_p) *diff_count_p = diff_count;
		return 0;
	} else {
		/* Different order - check if all registers exist in both arrays */
		int all_present = 1;
		
		/* Check if all registers from left exist in right */
		for (i = 0; i < (int)num1; i++) {
			int j, found = 0;
			for (j = 0; j < (int)num2; j++) {
				if (cfg1[i].reg == cfg2[j].reg) {
					found = 1;
					break;
				}
			}
			if (!found) {
				if (all_present) {
					print_error(indent, "Arrays have same length but different register sets!");
					print_info(indent, "Registers in LEFT but not in RIGHT:");
					all_present = 0;
				}
				printf("%s    " FMT_PHY_ENTRY "\n", indent, i, cfg1[i].reg, cfg1[i].val);
			}
		}
		
		/* Check if all registers from right exist in left */
		for (i = 0; i < (int)num2; i++) {
			int j, found = 0;
			for (j = 0; j < (int)num1; j++) {
				if (cfg2[i].reg == cfg1[j].reg) {
					found = 1;
					break;
				}
			}
			if (!found) {
				if (all_present) {
					print_error(indent, "Arrays have same length but different register sets!");
					all_present = 0;
				}
				if (!all_present && i == 0) {
					print_info(indent, "Registers in RIGHT but not in LEFT:");
				}
				printf("%s    " FMT_PHY_ENTRY "\n", indent, i, cfg2[i].reg, cfg2[i].val);
			}
		}
		
		if (!all_present) {
			/* Different register sets - structural error */
			return -1;
		}
		
		/* All registers present but different order - print warning first, then analyze with LCS-based diff */
		print_warning(indent, "Registers match, different order");
		print_reorder_header(indent);
		
		/* Use LCS (Longest Common Subsequence) approach to find matching blocks */
		int i1 = 0, i2 = 0;
		
		while (i1 < (int)num1 && i2 < (int)num2) {
			/* Check if registers at current positions match */
			if (cfg1[i1].reg == cfg2[i2].reg) {
				/* Found a matching block - scan forward to find the extent */
				int start_i1 = i1;
				int start_i2 = i2;
				
				while (i1 < (int)num1 && i2 < (int)num2 && cfg1[i1].reg == cfg2[i2].reg) {
					i1++;
					i2++;
				}
				
#if SHOW_IDENTICAL_RANGES
				/* Only show if more than a few registers to reduce noise */
				if (i1 - start_i1 > 10) {
					printf("%s  [%4d-%4d] (%d registers)           [%4d-%4d] (%d registers)\n",
					       indent, start_i1, i1 - 1, i1 - start_i1, start_i2, i2 - 1, i2 - start_i2);
				}
#else
				(void)start_i1; (void)start_i2; /* Unused when SHOW_IDENTICAL_RANGES is 0 */
#endif
			} else {
				/* Registers don't match - find where blocks appear */
				int block_start_i1 = i1;
				
				/* Scan forward in left to find extent of mismatched block */
				while (i1 < (int)num1) {
					/* Check if this register from left appears soon in right */
					int found_soon = 0;
					for (int check_j = i2; check_j < i2 + 50 && check_j < (int)num2; check_j++) {
						if (cfg1[i1].reg == cfg2[check_j].reg) {
							found_soon = 1;
							break;
						}
					}
					if (found_soon) break;
					i1++;
				}
				
				int block_start_i2 = i2;
				
				/* Scan forward in right to find extent of mismatched block */
				while (i2 < (int)num2) {
					/* Check if this register from right appears soon in left */
					int found_soon = 0;
					for (int check_i = i1; check_i < i1 + 50 && check_i < (int)num1; check_i++) {
						if (cfg2[i2].reg == cfg1[check_i].reg) {
							found_soon = 1;
							break;
						}
					}
					if (found_soon) break;
					i2++;
				}
				
				/* Display the relocated blocks side-by-side */
				if (block_start_i1 < i1 && block_start_i2 < i2) {
					/* Both sides have blocks - they're relocated */
					int left_count = i1 - block_start_i1;
					int right_count = i2 - block_start_i2;
					int max_show = (left_count < right_count) ? right_count : left_count;
					if (max_show > 10) max_show = 10;
					
					for (int k = 0; k < max_show; k++) {
						char left_buf[PHY_COLUMN_WIDTH] = "";
						char right_buf[PHY_COLUMN_WIDTH] = "";
						
						if (k < left_count) {
							snprintf(left_buf, sizeof(left_buf), FMT_PHY_ENTRY_4,
							         block_start_i1 + k, cfg1[block_start_i1 + k].reg, cfg1[block_start_i1 + k].val);
						}
						if (k < right_count) {
							snprintf(right_buf, sizeof(right_buf), FMT_PHY_ENTRY_4,
							         block_start_i2 + k, cfg2[block_start_i2 + k].reg, cfg2[block_start_i2 + k].val);
						}
						
						print_side_by_side(left_buf, right_buf, indent, PHY_COLUMN_WIDTH);
					}
					
					if (left_count > 10 || right_count > 10) {
						char left_more[PHY_COLUMN_WIDTH] = "";
						char right_more[PHY_COLUMN_WIDTH] = "";
						if (left_count > 10) {
							snprintf(left_more, sizeof(left_more), "... (%d more)", left_count - 10);
						}
						if (right_count > 10) {
							snprintf(right_more, sizeof(right_more), "... (%d more)", right_count - 10);
						}
						print_side_by_side(left_more, right_more, indent, PHY_COLUMN_WIDTH);
					}
				} else if (block_start_i1 < i1) {
					/* Only left has block */
					int left_count = i1 - block_start_i1;
					int show_count = (left_count < 10) ? left_count : 10;
					
					for (int k = 0; k < show_count; k++) {
						char left_buf[PHY_COLUMN_WIDTH];
						snprintf(left_buf, sizeof(left_buf), FMT_PHY_ENTRY_4,
						         block_start_i1 + k, cfg1[block_start_i1 + k].reg, cfg1[block_start_i1 + k].val);
						print_side_by_side(left_buf, "", indent, PHY_COLUMN_WIDTH);
					}
					if (left_count > 10) {
						print_side_by_side("...", "", indent, PHY_COLUMN_WIDTH);
						printf(" (%d more)\n", left_count - 10);
					}
				} else if (block_start_i2 < i2) {
					/* Only right has block */
					int right_count = i2 - block_start_i2;
					int show_count = (right_count < 10) ? right_count : 10;
					
					for (int k = 0; k < show_count; k++) {
						char right_buf[PHY_COLUMN_WIDTH];
						snprintf(right_buf, sizeof(right_buf), FMT_PHY_ENTRY_4,
						         block_start_i2 + k, cfg2[block_start_i2 + k].reg, cfg2[block_start_i2 + k].val);
						print_side_by_side("", right_buf, indent, PHY_COLUMN_WIDTH);
					}
					if (right_count > 10) {
						print_side_by_side("", "...", indent, PHY_COLUMN_WIDTH);
						printf(" (%d more)\n", right_count - 10);
					}
				}
			}
		}
		
		/* Handle remaining registers at the end */
		if (i1 < (int)num1) {
			int remain_count = (int)num1 - i1;
			int show_count = (remain_count < 10) ? remain_count : 10;
			for (int k = 0; k < show_count; k++) {
				char left_buf[PHY_COLUMN_WIDTH];
				snprintf(left_buf, sizeof(left_buf), FMT_PHY_ENTRY_4,
				         i1 + k, cfg1[i1 + k].reg, cfg1[i1 + k].val);
				print_side_by_side(left_buf, "", indent, PHY_COLUMN_WIDTH);
			}
			if (remain_count > 10) {
				print_side_by_side("...", "", indent, PHY_COLUMN_WIDTH);
				printf(" (%d more)\n", remain_count - 10);
			}
		}
		if (i2 < (int)num2) {
			int remain_count = (int)num2 - i2;
			int show_count = (remain_count < 10) ? remain_count : 10;
			for (int k = 0; k < show_count; k++) {
				char right_buf[PHY_COLUMN_WIDTH];
				snprintf(right_buf, sizeof(right_buf), FMT_PHY_ENTRY_4,
				         i2 + k, cfg2[i2 + k].reg, cfg2[i2 + k].val);
				print_side_by_side("", right_buf, indent, PHY_COLUMN_WIDTH);
			}
			if (remain_count > 10) {
				print_side_by_side("", "...", indent, PHY_COLUMN_WIDTH);
				printf(" (%d more)\n", remain_count - 10);
			}
		}
		
		/* Count value differences first */
		for (i = 0; i < (int)num1; i++) {
			/* Find matching register in cfg2 */
			int j;
			for (j = 0; j < (int)num2; j++) {
				if (cfg1[i].reg == cfg2[j].reg) {
					if (cfg1[i].val != cfg2[j].val) {
						diff_count++;
					}
					break;
				}
			}
		}
		
		/* Print summary and details for value differences */
		if (diff_count > 0) {
			print_info(indent, "Value differences: %d", diff_count);
			print_info(indent, "Register value differences:");
			/* Now print the details */
			for (i = 0; i < (int)num1; i++) {
				int j;
				for (j = 0; j < (int)num2; j++) {
					if (cfg1[i].reg == cfg2[j].reg) {
						if (cfg1[i].val != cfg2[j].val) {
							printf("%s    " FMT_PHY_DIFF_4 "\n", 
							       indent, i, cfg1[i].reg, cfg1[i].val, cfg2[j].val);
						}
						break;
					}
				}
			}
		}
		
		/* Return: 2 for different order (diff_count tracks value differences) */
		if (diff_count_p) *diff_count_p = diff_count;
		
		return 1;
	}
}

/**
 * @brief Compare ddrc_cfg structures between left and right configurations
 * 
 * @return int 0 on success, negative on error
 */
static int check_ddrc_cfg(void) {
	int result;
	int diff_count = 0;
	
	printf("┌─────────────────────────────────────────────────────────────────────────┐\n");
	printf("│ Checking ddrc_cfg                                                       │\n");
	printf("└─────────────────────────────────────────────────────────────────────────┘\n");
	
	result = compare_ddrc_cfg_arrays(dram_timing_left.ddrc_cfg, dram_timing_left.ddrc_cfg_num,
	                                  dram_timing_right.ddrc_cfg, dram_timing_right.ddrc_cfg_num,
	                                  "  ", &diff_count, 1);
	
	print_comparison_summary(result, diff_count, "  ");
	
	/* Check duplicates after comparison summary */
	struct duplicate_info left_dups[100];
	struct duplicate_info right_dups[100];
	int left_dup_count = find_duplicates_ddrc(dram_timing_left.ddrc_cfg, dram_timing_left.ddrc_cfg_num,
	                                           left_dups, 100);
	int right_dup_count = find_duplicates_ddrc(dram_timing_right.ddrc_cfg, dram_timing_right.ddrc_cfg_num,
	                                            right_dups, 100);
	
	if (left_dup_count > 0 || right_dup_count > 0) {
		/* Check for interference between duplicates and value differences */
		if (result >= 0 && diff_count > 0) {
			/* Only check if same size and there are value differences */
			if (dram_timing_left.ddrc_cfg_num == dram_timing_right.ddrc_cfg_num) {
				check_duplicate_interference(dram_timing_left.ddrc_cfg, dram_timing_right.ddrc_cfg,
				                           dram_timing_left.ddrc_cfg_num, 
				                           left_dups, left_dup_count, 
				                           right_dups, right_dup_count, "  ", 1);
			}
		}
		
		if (opt_list_duplicates) {
			/* Show detailed list */
			print_duplicates_ddrc_sidebyside(left_dups, left_dup_count, right_dups, right_dup_count, "  ");
		} else {
			/* Show summary only */
			int total = left_dup_count + right_dup_count;
			print_info("  ", "Duplicate registers found: %d (use --list-duplicates for details)", total);
		}
	}
	
	printf("\n");
	
	return 0;  /* Always return success - differences are informational */
}

/**
 * @brief Compare fsp_cfg structures between left and right configurations
 * 
 * @return int 0 on success, negative on error
 */
static int check_fsp_cfg(void) {
	int i;
	int ret = 0;
	int total_diff_count = 0;
	
	printf("┌─────────────────────────────────────────────────────────────────────────┐\n");
	printf("│ Checking fsp_cfg                                                        │\n");
	printf("└─────────────────────────────────────────────────────────────────────────┘\n");
	printf("  FSP Entries: Left=%u, Right=%u\n", 
	       dram_timing_left.fsp_cfg_num, dram_timing_right.fsp_cfg_num);
	
	if (dram_timing_left.fsp_cfg_num != dram_timing_right.fsp_cfg_num) {
		print_error("  ", "Number of FSP entries do not match!");
		printf("\n");
		return -1;
	}
	
	for (i = 0; i < (int)dram_timing_left.fsp_cfg_num; i++) {
		int fsp_result;
		int fsp_diff_count = 0;
		
		printf("\n  FSP %d:\n", i);
		printf("  ┌─── ddrc_cfg ─────────────────────────────────────────────────────┐\n");
		fsp_result = compare_ddrc_cfg_arrays(
			dram_timing_left.fsp_cfg[i].ddrc_cfg, dram_timing_left.fsp_cfg[i].ddrc_cfg_num,
			dram_timing_right.fsp_cfg[i].ddrc_cfg, dram_timing_right.fsp_cfg[i].ddrc_cfg_num,
			"    ", &fsp_diff_count, 1);
		
		if (fsp_result < 0) {
			ret = -1;
		}
		
		/* Check bypass */
		if (dram_timing_left.fsp_cfg[i].bypass != dram_timing_right.fsp_cfg[i].bypass) {
			printf("    bypass: %u → %u\n",
			       dram_timing_left.fsp_cfg[i].bypass,
			       dram_timing_right.fsp_cfg[i].bypass);
			fsp_diff_count++;
		}
		
		printf("  └──────────────────────────────────────────────────────────────────┘\n");
		
		total_diff_count += fsp_diff_count;
	}
	
	printf("\n");
	
	return ret;
}

/**
 * @brief Compare ddrphy_cfg structures between left and right configurations
 * 
 * @return int 0 on success, negative on error
 */
static int check_ddrphy_cfg(void) {
	int result;
	int diff_count = 0;
	
	printf("┌─────────────────────────────────────────────────────────────────────────┐\n");
	printf("│ Checking ddrphy_cfg                                                     │\n");
	printf("└─────────────────────────────────────────────────────────────────────────┘\n");
	
	result = compare_ddrphy_cfg_arrays(dram_timing_left.ddrphy_cfg, dram_timing_left.ddrphy_cfg_num,
	                                    dram_timing_right.ddrphy_cfg, dram_timing_right.ddrphy_cfg_num,
	                                    "  ", &diff_count, 1);
	
	print_comparison_summary(result, diff_count, "  ");
	printf("\n");
	
	return 0;  /* Always return success - differences are informational */
}

/**
 * @brief Compare fsp_msg structures between left and right configurations
 * 
 * @return int 0 on success, negative on error
 */
static int check_fsp_msg(void) {
	int i;
	int ret = 0;
	int total_diff_count = 0;
	
	printf("┌─────────────────────────────────────────────────────────────────────────┐\n");
	printf("│ Checking fsp_msg                                                        │\n");
	printf("└─────────────────────────────────────────────────────────────────────────┘\n");
	printf("  FSP Message Entries: Left=%u, Right=%u\n", 
	       dram_timing_left.fsp_msg_num, dram_timing_right.fsp_msg_num);
	
	if (dram_timing_left.fsp_msg_num != dram_timing_right.fsp_msg_num) {
		print_error("  ", "Number of FSP message entries do not match!");
		printf("\n");
		return -1;
	}
	
	for (i = 0; i < (int)dram_timing_left.fsp_msg_num; i++) {
		int result;
		int diff_count;
		
		printf("\n  FSP Message %d:\n", i);
		
		/* Check drate */
		if (dram_timing_left.fsp_msg[i].drate != dram_timing_right.fsp_msg[i].drate) {
			printf("    drate: %u → %u\n",
			       dram_timing_left.fsp_msg[i].drate,
			       dram_timing_right.fsp_msg[i].drate);
			total_diff_count++;
		}
		
		/* Check fw_type */
		if (dram_timing_left.fsp_msg[i].fw_type != dram_timing_right.fsp_msg[i].fw_type) {
			printf("    fw_type: %d → %d\n",
			       dram_timing_left.fsp_msg[i].fw_type,
			       dram_timing_right.fsp_msg[i].fw_type);
			total_diff_count++;
		}
		
		/* Check fsp_phy_cfg */
		printf("\n");
		printf("    ┌─── fsp_phy_cfg ──────────────────────────────────────────────┐\n");
		
		result = compare_ddrphy_cfg_arrays(
			dram_timing_left.fsp_msg[i].fsp_phy_cfg, dram_timing_left.fsp_msg[i].fsp_phy_cfg_num,
			dram_timing_right.fsp_msg[i].fsp_phy_cfg, dram_timing_right.fsp_msg[i].fsp_phy_cfg_num,
			"      ", &diff_count, 1);
		
		if (result < 0) {
			ret = -1;
		}
		total_diff_count += diff_count;
		print_comparison_summary(result, diff_count, "      ");
		printf("    └──────────────────────────────────────────────────────────────┘\n");
		
		/* Check fsp_phy_msgh_cfg */
		printf("\n");
		printf("    ┌─── fsp_phy_msgh_cfg ─────────────────────────────────────────┐\n");
		
		result = compare_ddrphy_cfg_arrays(
			dram_timing_left.fsp_msg[i].fsp_phy_msgh_cfg, dram_timing_left.fsp_msg[i].fsp_phy_msgh_cfg_num,
			dram_timing_right.fsp_msg[i].fsp_phy_msgh_cfg, dram_timing_right.fsp_msg[i].fsp_phy_msgh_cfg_num,
			"      ", &diff_count, 1);
		
		if (result < 0) {
			ret = -1;
		}
		total_diff_count += diff_count;
		print_comparison_summary(result, diff_count, "      ");
		printf("    └──────────────────────────────────────────────────────────────┘\n");
		
		/* Check fsp_phy_pie_cfg */
		printf("\n");
		printf("    ┌─── fsp_phy_pie_cfg ──────────────────────────────────────────┐\n");
		
		result = compare_ddrphy_cfg_arrays(
			dram_timing_left.fsp_msg[i].fsp_phy_pie_cfg, dram_timing_left.fsp_msg[i].fsp_phy_pie_cfg_num,
			dram_timing_right.fsp_msg[i].fsp_phy_pie_cfg, dram_timing_right.fsp_msg[i].fsp_phy_pie_cfg_num,
			"      ", &diff_count, 1);
		
		if (result < 0) {
			ret = -1;
		}
		total_diff_count += diff_count;
		print_comparison_summary(result, diff_count, "      ");
		printf("    └──────────────────────────────────────────────────────────────┘\n");
	}
	
	if (ret != 0) {
		print_warning("\n  ", "Structural errors found");
	}
	printf("\n");
	
	return ret;
}

/**
 * @brief Compare ddrphy_trained_csr structures between left and right configurations
 * 
 * @return int 0 on success, negative on error
 */
static int check_ddrphy_trained_csr(void) {
	int result;
	int diff_count = 0;
	
	printf("┌─────────────────────────────────────────────────────────────────────────┐\n");
	printf("│ Checking ddrphy_trained_csr                                             │\n");
	printf("└─────────────────────────────────────────────────────────────────────────┘\n");
	
	result = compare_ddrphy_cfg_arrays(dram_timing_left.ddrphy_trained_csr, dram_timing_left.ddrphy_trained_csr_num,
	                                    dram_timing_right.ddrphy_trained_csr, dram_timing_right.ddrphy_trained_csr_num,
	                                    "  ", &diff_count, 1);
	
	print_comparison_summary(result, diff_count, "  ");
	printf("\n");
	
	return 0;  /* Always return success - differences are informational */
}

/**
 * @brief Compare ddrphy_pie structures between left and right configurations
 * 
 * @return int 0 on success, negative on error
 */
static int check_ddrphy_pie(void) {
	int result;
	int diff_count = 0;
	
	printf("┌─────────────────────────────────────────────────────────────────────────┐\n");
	printf("│ Checking ddrphy_pie                                                     │\n");
	printf("└─────────────────────────────────────────────────────────────────────────┘\n");
	
	result = compare_ddrphy_cfg_arrays(dram_timing_left.ddrphy_pie, dram_timing_left.ddrphy_pie_num,
	                                    dram_timing_right.ddrphy_pie, dram_timing_right.ddrphy_pie_num,
	                                    "  ", &diff_count, 1);
	
	print_comparison_summary(result, diff_count, "  ");
	
	/* Check duplicates after comparison summary */
	struct duplicate_info left_dups[100];
	struct duplicate_info right_dups[100];
	int left_dup_count = find_duplicates_ddrphy(dram_timing_left.ddrphy_pie, dram_timing_left.ddrphy_pie_num,
	                                             left_dups, 100);
	int right_dup_count = find_duplicates_ddrphy(dram_timing_right.ddrphy_pie, dram_timing_right.ddrphy_pie_num,
	                                              right_dups, 100);
	
	if (left_dup_count > 0 || right_dup_count > 0) {
		/* Check for interference between duplicates and value differences */
		if (result >= 0 && diff_count > 0) {
			/* Only check if same size and there are value differences */
			if (dram_timing_left.ddrphy_pie_num == dram_timing_right.ddrphy_pie_num) {
				check_duplicate_interference(dram_timing_left.ddrphy_pie, dram_timing_right.ddrphy_pie,
				                           dram_timing_left.ddrphy_pie_num, 
				                           left_dups, left_dup_count, 
				                           right_dups, right_dup_count, "  ", 0);
			}
		}
		
		if (opt_list_duplicates) {
			/* Show detailed list */
			print_duplicates_ddrphy_sidebyside(left_dups, left_dup_count, right_dups, right_dup_count, "  ");
		} else {
			/* Show summary only */
			int total = left_dup_count + right_dup_count;
			print_info("  ", "Duplicate registers found: %d (use --list-duplicates for details)", total);
		}
	}
	
	printf("\n");
	
	return 0;  /* Always return success - differences are informational */
}

int main(int argc, char *argv[]) {
	int ret = 0;
	
	/* Parse command-line arguments */
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--list-duplicates") == 0) {
			opt_list_duplicates = 1;
		} else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("Usage: %s [OPTIONS]\n", argv[0]);
			printf("Options:\n");
			printf("  --list-duplicates  Show detailed list of duplicate registers\n");
			printf("  --help, -h         Show this help message\n");
			return 0;
		} else {
			fprintf(stderr, "Unknown option: %s\n", argv[i]);
			fprintf(stderr, "Use --help for usage information\n");
			return 1;
		}
	}
	
	printf("\n");
	printf("═══════════════════════════════════════════════════════════════════════════\n");
	printf("                    DDR Configuration Comparison Tool                      \n");
	printf("═══════════════════════════════════════════════════════════════════════════\n");
	printf("\n");
	
	/** DDRC configurations */
	ret = check_ddrc_cfg();
	ret |= check_fsp_cfg();

	/** DDR PHY configurations */
	ret |= check_ddrphy_cfg();
	ret |= check_fsp_msg();
	ret |= check_ddrphy_trained_csr();
	ret |= check_ddrphy_pie();
	
	/* Calculate and print total sizes */
	printf("┌─────────────────────────────────────────────────────────────────────────┐\n");
	printf("│ Total Configuration Sizes                                               │\n");
	printf("└─────────────────────────────────────────────────────────────────────────┘\n");
	
	unsigned int left_total = 0;
	unsigned int right_total = 0;
	
	/* ddrc_cfg */
	left_total += dram_timing_left.ddrc_cfg_num * sizeof(struct ddrc_cfg_param);
	right_total += dram_timing_right.ddrc_cfg_num * sizeof(struct ddrc_cfg_param);
	
	/* fsp_cfg */
	for (unsigned int i = 0; i < dram_timing_left.fsp_cfg_num; i++) {
		left_total += dram_timing_left.fsp_cfg[i].ddrc_cfg_num * sizeof(struct ddrc_cfg_param);
	}
	for (unsigned int i = 0; i < dram_timing_right.fsp_cfg_num; i++) {
		right_total += dram_timing_right.fsp_cfg[i].ddrc_cfg_num * sizeof(struct ddrc_cfg_param);
	}
	
	/* ddrphy_cfg */
	left_total += dram_timing_left.ddrphy_cfg_num * sizeof(struct ddrphy_cfg_param);
	right_total += dram_timing_right.ddrphy_cfg_num * sizeof(struct ddrphy_cfg_param);
	
	/* fsp_msg */
	for (unsigned int i = 0; i < dram_timing_left.fsp_msg_num; i++) {
		left_total += dram_timing_left.fsp_msg[i].fsp_phy_cfg_num * sizeof(struct ddrphy_cfg_param);
		left_total += dram_timing_left.fsp_msg[i].fsp_phy_msgh_cfg_num * sizeof(struct ddrphy_cfg_param);
		left_total += dram_timing_left.fsp_msg[i].fsp_phy_pie_cfg_num * sizeof(struct ddrphy_cfg_param);
	}
	for (unsigned int i = 0; i < dram_timing_right.fsp_msg_num; i++) {
		right_total += dram_timing_right.fsp_msg[i].fsp_phy_cfg_num * sizeof(struct ddrphy_cfg_param);
		right_total += dram_timing_right.fsp_msg[i].fsp_phy_msgh_cfg_num * sizeof(struct ddrphy_cfg_param);
		right_total += dram_timing_right.fsp_msg[i].fsp_phy_pie_cfg_num * sizeof(struct ddrphy_cfg_param);
	}
	
	/* ddrphy_trained_csr */
	left_total += dram_timing_left.ddrphy_trained_csr_num * sizeof(struct ddrphy_cfg_param);
	right_total += dram_timing_right.ddrphy_trained_csr_num * sizeof(struct ddrphy_cfg_param);
	
	/* ddrphy_pie */
	left_total += dram_timing_left.ddrphy_pie_num * sizeof(struct ddrphy_cfg_param);
	right_total += dram_timing_right.ddrphy_pie_num * sizeof(struct ddrphy_cfg_param);
	
	printf("  Left:  %u bytes (%.2f kB)\n", left_total, left_total / 1024.0);
	printf("  Right: %u bytes (%.2f kB)\n", right_total, right_total / 1024.0);
	if (left_total != right_total) {
		int diff = (int)right_total - (int)left_total;
		printf("  Difference: %+d bytes (%+.2f kB)\n", diff, diff / 1024.0);
	}
	printf("\n");
	
	printf("═══════════════════════════════════════════════════════════════════════════\n");
	print_info("                      ", "COMPARISON COMPLETE");
	printf("═══════════════════════════════════════════════════════════════════════════\n");
	printf("\n");
	
	return 0;  /* Always return success - comparison completed successfully */
}
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>

