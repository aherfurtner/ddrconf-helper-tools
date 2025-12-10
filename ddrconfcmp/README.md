# DDR Configuration Comparison Tool

A tool to compare DDR memory configurations across different memory sizes for the DART-MX95 platform.

## Features

- Compares DDR register configurations between different memory sizes (2GB, 4GB, 8GB, 16GB)
- Identifies structural differences (different register sets or counts)
- Detects value differences in matching registers
- Highlights registers with different ordering
- Provides detailed analysis with visual hierarchy using box formatting
- Generates comparison reports saved to text files
- Supports multiple firmware versions
- Automatically skips missing configurations

## Building and Running

### Default Configuration

By default, the tool compares 4GB (base) against all other available sizes in v25.09:

```bash
make
```

This will generate output files in the `output/` directory:
- `v25.09_4GB_vs_2GB.txt`
- `v25.09_4GB_vs_8GB.txt`
- `v25.09_4GB_vs_16GB.txt`

### Custom Configuration

You can override the default settings using command-line variables:

```bash
# Use a different firmware version
make VERSION=v25.06

# Use a different base size
make BASE_SIZE=8GB

# Compare only specific sizes
make ALL_SIZES="4GB 8GB"

# Combine multiple overrides
make VERSION=v25.06 BASE_SIZE=8GB ALL_SIZES="4GB 16GB"
```

### Available Variables

- `VERSION`: Firmware version (default: `v25.09`)
  - Available: `v25.06`, `v25.09`
- `BASE_SIZE`: Base configuration to compare against (default: `4GB`)
  - Options: `2GB`, `4GB`, `8GB`, `16GB`
- `ALL_SIZES`: All sizes to compare (default: `2GB 4GB 8GB 16GB`)
  - Can be any subset of the available sizes

### Cleaning

Remove generated files:

```bash
make clean
```

This removes:
- The compiled binary (`ddrconfcmp`)
- Temporary files (`lpddr5_timing_left.c`, `lpddr5_timing_right.c`)
- Output directory (`output/`)

## Output Format

The tool provides detailed comparison results with color-coded output:

- **Green**: Successful checks, matching registers
- **Yellow (W:)**: Warnings - registers match but have value differences or different ordering
- **Yellow (I:)**: Informational messages
- **Red (E:)**: Errors - structural differences detected

### Output Sections

1. **ddrc_cfg**: DDR controller base configuration
2. **fsp_cfg**: Frequency Set Point configurations
3. **ddrphy_cfg**: DDR PHY base configuration
4. **fsp_msg**: FSP message configurations
   - `fsp_phy_cfg`: PHY configuration per FSP
   - `fsp_phy_msgh_cfg`: PHY message header configuration
   - `fsp_phy_pie_cfg`: PHY PIE configuration
5. **ddrphy_trained_csr**: Trained CSR values
6. **ddrphy_pie**: PHY PIE values
7. **Total Configuration Sizes**: Summary of total memory usage

### Special Features

- **Unique Registers**: Displayed side-by-side in LEFT/RIGHT columns
- **Common Register Comparison**: When array lengths differ, compares common registers separately
- **Nested Boxes**: Visual hierarchy with box drawing characters for sub-structures
- **Value Differences**: Shows register value changes (e.g., `0x045c → 0x041c`)

## Directory Structure

```
ddrconfcmp/
├── Makefile           # Build configuration
├── README.md          # This file
├── ddrconfcmp.c       # Main comparison tool
├── output/            # Generated comparison reports (created on first run)
└── ../configs/        # Configuration files (shared with parent directory)
    ├── v25.06/
    │   ├── DART-MX95_4GB/
    │   ├── DART-MX95_8GB/
    │   └── DART-MX95_16GB/
    └── v25.09/
        ├── DART-MX95_2GB/
        ├── DART-MX95_4GB/
        ├── DART-MX95_8GB/
        └── DART-MX95_16GB/
```

## Examples

### Compare 4GB against all sizes in v25.09
```bash
make
```

### Compare 8GB against 4GB and 16GB in v25.06
```bash
make VERSION=v25.06 BASE_SIZE=8GB ALL_SIZES="4GB 16GB"
```

### Compare only 4GB vs 8GB in current version
```bash
make ALL_SIZES="8GB"
```

## Return Codes

The tool uses the following return codes:

- `0`: All configurations match (no differences)
- `255`: Some checks failed (differences found or structural errors)

The tool continues processing all comparisons even if some fail (`|| true` in Makefile).

## Technical Details

- **Language**: C (C99 standard)
- **Compiler**: GCC with `-Wall -Wextra` flags
- **ANSI Colors**: Used for formatted output
- **Box Drawing**: Unicode box-drawing characters for visual hierarchy
- **Memory Management**: Proper malloc/free for temporary arrays during recursive comparison

## Notes

- Configuration files must be named `lpddr5_timing.c` and located in the appropriate directory structure
- Missing configurations are automatically skipped with an informational message
- Output files are overwritten on each run
- Temporary files are cleaned up automatically after all comparisons complete
