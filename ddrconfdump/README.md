# DDR Configuration Dump Tool

This tool dumps DDR memory configurations for the DART-MX95 platform in a structured format with CRC32 checksums for verification.

## Features

- Dumps all DDR configuration arrays (ddrc_cfg, fsp_cfg, ddrphy_cfg, fsp_msg, ddrphy_trained_csr, ddrphy_pie)
- Calculates CRC32 checksums for each configuration array
- Outputs in a structured, parseable format
- Supports multiple memory sizes (2GB, 4GB, 8GB, 16GB)
- Supports multiple configuration versions (v25.06, v25.09, etc.)

## Building and Running

### Dump all configurations for default version (v25.09):
```bash
make
```

### Dump configurations for a specific version:
```bash
make VERSION=v25.06
```

### Dump only specific memory sizes:
```bash
make ALL_SIZES="4GB 8GB"
```

### Combined example:
```bash
make VERSION=v25.06 ALL_SIZES="4GB 16GB"
```

### Clean output:
```bash
make clean
```

## Output Format

Each configuration array is dumped in the following format:

```
<array_name>
entries=<count>, size=<bytes>
crc32=0x<checksum>
[<index>]={<reg_offset>, <reg_value>}
[<index>]={<reg_offset>, <reg_value>}
...
```

Indices are right-aligned with 4-digit width for consistent formatting.

### Example Output:

```
ddrc_cfg
entries=37, size=296 bytes
crc32=0x3b490047
[   0]={0x5e080110, 0x41110001}
[   1]={0x5e080000, 0x0000007f}
[   2]={0x5e080008, 0x00000000}
[   9]={0x5e080080, 0x80800322}
[  10]={0x5e080084, 0x00000000}
...

fsp_cfg[0].ddrc_cfg
entries=18, size=144 bytes
crc32=0x114c6b30
[   0]={0x5e080100, 0x020a2100}
[   1]={0x5e080104, 0x0066000c}
...

ddrphy_cfg
entries=87, size=522 bytes
crc32=0x7ca176d5
[   0]={0x10080, 0x0003}
[   1]={0x10081, 0x0002}
...
```

## Output Directory Structure

All dumps are saved in the `output/` directory with the naming convention:
```
output/<VERSION>_<SIZE>.txt
```

Examples:
- `output/v25.09_4GB.txt`
- `output/v25.06_16GB.txt`

## Configuration Arrays Dumped

The tool dumps the following configuration arrays:

1. **ddrc_cfg** - DDR controller base configuration
2. **fsp_cfg[n].ddrc_cfg** - Frequency Set Point DDRC configurations
3. **fsp_cfg[n].bypass** - FSP bypass values
4. **ddrphy_cfg** - DDR PHY base configuration
5. **fsp_msg[n].drate** - FSP message data rate
6. **fsp_msg[n].fw_type** - FSP message firmware type
7. **fsp_msg[n].fsp_phy_cfg** - FSP PHY configurations
8. **fsp_msg[n].fsp_phy_msgh_cfg** - FSP PHY message header configurations
9. **fsp_msg[n].fsp_phy_pie_cfg** - FSP PHY PIE configurations
10. **ddrphy_trained_csr** - Trained CSR values
11. **ddrphy_pie** - PHY PIE values

## CRC32 Checksums

Each configuration array includes a CRC32 checksum calculated using a lookup table-based algorithm. This allows for:
- Quick verification of configuration integrity
- Detection of configuration changes
- Comparison between different memory sizes or versions

## Available Configurations

### v25.09:
- DART-MX95_2GB
- DART-MX95_4GB
- DART-MX95_8GB
- DART-MX95_16GB

### v25.06:
- DART-MX95_4GB
- DART-MX95_8GB
- DART-MX95_16GB

## Technical Details

- **Language**: C99
- **Compiler**: GCC with `-Wall -Wextra`
- **Register Format**: 
  - DDRC registers: 32-bit address, 32-bit value (0x%08x format)
  - DDRPHY registers: 20-bit address, 16-bit value (0x%05x/0x%04x format)

## Use Cases

1. **Configuration Verification**: Use CRC32 checksums to verify configuration integrity
2. **Configuration Analysis**: Parse dump files to analyze register values
3. **Configuration Comparison**: Compare dumps between different memory sizes
4. **Documentation**: Generate human-readable configuration documentation
5. **Debugging**: Export configurations for analysis in external tools
