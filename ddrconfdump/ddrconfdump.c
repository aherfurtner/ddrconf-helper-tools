/**
 * @file ddrconfdump.c
 * @brief DDR Configuration Dump Tool
 * 
 * This tool dumps DDR memory configurations for the DART-MX95 platform
 * in a structured format with CRC32 checksums for verification.
 * 
 * Output format for each array:
 *   <name of the table>
 *   entries=<count>, size=<bytes>
 *   crc32=0x<checksum>
 *   <reg offset> <reg value>
 *   ...
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "ddr.h"

/* The timing configuration will be included at compile time */
#include "lpddr5_timing.c"

/**
 * @brief Calculate CRC32 checksum
 * 
 * CRC32 algorithm using lookup table
 * 
 * @param addr Pointer to data buffer
 * @param size Length of data in bytes
 * @return uint32_t CRC32 checksum
 */
static uint32_t crc32(const uint8_t *addr, uint32_t size) {
	const uint8_t *a = addr;
	uint32_t sz = size;
	uint32_t crc = 0U;
	
	/* Poly table */
	static uint32_t const s_crcTable[] = {
		0x4DBDF21CU, 0x500AE278U, 0x76D3D2D4U, 0x6B64C2B0U,
		0x3B61B38CU, 0x26D6A3E8U, 0x000F9344U, 0x1DB88320U,
		0xA005713CU, 0xBDB26158U, 0x9B6B51F4U, 0x86DC4190U,
		0xD6D930ACU, 0xCB6E20C8U, 0xEDB71064U, 0xF0000000U
	};
	
	/* Loop over data */
	while (sz > 0U) {
		crc = (crc >> 4U) ^ s_crcTable[(crc ^ (((uint32_t)(*a)) >> 0U)) & 0x0FU];
		crc = (crc >> 4U) ^ s_crcTable[(crc ^ (((uint32_t)(*a)) >> 4U)) & 0x0FU];
		a++;
		sz--;
	}
	
	/* Return CRC */
	return crc;
}

/**
 * @brief Dump ddrc_cfg_param array
 * 
 * @param name Array name for display
 * @param cfg Configuration array
 * @param num Number of entries
 */
static void dump_ddrc_cfg_array(const char *name, const struct ddrc_cfg_param *cfg, unsigned int num) {
	if (!cfg || num == 0) {
		return;
	}
	
	size_t size = num * sizeof(struct ddrc_cfg_param);
	uint32_t checksum = crc32((const uint8_t *)cfg, (uint32_t)size);
	
	printf("\n%s\n", name);
	printf("entries=%u, size=%zu bytes\n", num, size);
	printf("crc32=0x%08x\n", checksum);
	
	for (unsigned int i = 0; i < num; i++) {
		printf("[%4u]={0x%08x, 0x%08x}\n", i, cfg[i].reg, cfg[i].val);
	}
}

/**
 * @brief Dump ddrphy_cfg_param array
 * 
 * @param name Array name for display
 * @param cfg Configuration array
 * @param num Number of entries
 */
static void dump_ddrphy_cfg_array(const char *name, const struct ddrphy_cfg_param *cfg, unsigned int num) {
	if (!cfg || num == 0) {
		return;
	}
	
	size_t size = num * sizeof(struct ddrphy_cfg_param);
	uint32_t checksum = crc32((const uint8_t *)cfg, (uint32_t)size);
	
	printf("\n%s\n", name);
	printf("entries=%u, size=%zu bytes\n", num, size);
	printf("crc32=0x%08x\n", checksum);
	
	for (unsigned int i = 0; i < num; i++) {
		printf("[%4u]={0x%05x, 0x%04x}\n", i, cfg[i].reg, cfg[i].val);
	}
}

/**
 * @brief Dump ddrc_cfg configuration
 */
static void dump_ddrc_cfg(void) {
	dump_ddrc_cfg_array("ddrc_cfg", 
	                    dram_timing.ddrc_cfg,
	                    dram_timing.ddrc_cfg_num);
}

/**
 * @brief Dump fsp_cfg configurations
 */
static void dump_fsp_cfg(void) {
	for (unsigned int i = 0; i < dram_timing.fsp_cfg_num; i++) {
		char name[64];
		
		snprintf(name, sizeof(name), "fsp_cfg[%u].ddrc_cfg", i);
		dump_ddrc_cfg_array(name,
		                    dram_timing.fsp_cfg[i].ddrc_cfg,
		                    dram_timing.fsp_cfg[i].ddrc_cfg_num);
		
		/* Also dump bypass field */
		printf("\n");
		printf("fsp_cfg[%u].bypass=%u\n", i, dram_timing.fsp_cfg[i].bypass);
	}
}

/**
 * @brief Dump ddrphy_cfg configuration
 */
static void dump_ddrphy_cfg(void) {
	dump_ddrphy_cfg_array("ddrphy_cfg",
	                      dram_timing.ddrphy_cfg,
	                      dram_timing.ddrphy_cfg_num);
}

/**
 * @brief Dump fsp_msg configurations
 */
static void dump_fsp_msg(void) {
	for (unsigned int i = 0; i < dram_timing.fsp_msg_num; i++) {
		char name[64];
		
		/* Dump drate and fw_type */
		printf("\n");
		printf("fsp_msg[%u].drate=%u\n", i, dram_timing.fsp_msg[i].drate);
		printf("fsp_msg[%u].fw_type=%d\n", i, dram_timing.fsp_msg[i].fw_type);
		
		/* Dump fsp_phy_cfg */
		snprintf(name, sizeof(name), "fsp_msg[%u].fsp_phy_cfg", i);
		dump_ddrphy_cfg_array(name,
		                      dram_timing.fsp_msg[i].fsp_phy_cfg,
		                      dram_timing.fsp_msg[i].fsp_phy_cfg_num);
		
		/* Dump fsp_phy_msgh_cfg */
		snprintf(name, sizeof(name), "fsp_msg[%u].fsp_phy_msgh_cfg", i);
		dump_ddrphy_cfg_array(name,
		                      dram_timing.fsp_msg[i].fsp_phy_msgh_cfg,
		                      dram_timing.fsp_msg[i].fsp_phy_msgh_cfg_num);
		
		/* Dump fsp_phy_pie_cfg */
		snprintf(name, sizeof(name), "fsp_msg[%u].fsp_phy_pie_cfg", i);
		dump_ddrphy_cfg_array(name,
		                      dram_timing.fsp_msg[i].fsp_phy_pie_cfg,
		                      dram_timing.fsp_msg[i].fsp_phy_pie_cfg_num);
	}
}

/**
 * @brief Dump ddrphy_trained_csr configuration
 */
static void dump_ddrphy_trained_csr(void) {
	dump_ddrphy_cfg_array("ddrphy_trained_csr",
	                      dram_timing.ddrphy_trained_csr,
	                      dram_timing.ddrphy_trained_csr_num);
}

/**
 * @brief Dump ddrphy_pie configuration
 */
static void dump_ddrphy_pie(void) {
	dump_ddrphy_cfg_array("ddrphy_pie",
	                      dram_timing.ddrphy_pie,
	                      dram_timing.ddrphy_pie_num);
}

int main(void) {
	printf("═══════════════════════════════════════════════════════════════════════════\n");
	printf("                     DDR Configuration Dump Tool                           \n");
	printf("═══════════════════════════════════════════════════════════════════════════\n");
	
	/* Dump all configuration arrays */
	dump_ddrc_cfg();
	dump_fsp_cfg();
	dump_ddrphy_cfg();
	dump_fsp_msg();
	dump_ddrphy_trained_csr();
	dump_ddrphy_pie();
	
	printf("\n");
	printf("═══════════════════════════════════════════════════════════════════════════\n");
	printf("                              DUMP COMPLETE                                \n");
	printf("═══════════════════════════════════════════════════════════════════════════\n");
	printf("\n");
	
	return 0;
}
