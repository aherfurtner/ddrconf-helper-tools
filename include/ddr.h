/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright 2022-2024 NXP
 * Copyright 2025 Variscite Ltd.
 * 
 * External DDR configuration data structures extracted from imx-oei
 * project.
 */

#ifndef __DDR_H
#define __DDR_H
#include <stdbool.h>
#include <stdint.h>

#if !defined(ARRAY_SIZE)
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

/* user data type */
enum fw_type
{
    FW_1D_IMAGE,
    FW_2D_IMAGE,
};

struct ddrc_cfg_param
{
    unsigned int reg;
    unsigned int val;
};

struct __attribute__((__packed__)) ddrphy_cfg_param
{
    unsigned int   reg;
    unsigned short val;
};

struct dram_fsp_cfg
{
    struct ddrc_cfg_param *ddrc_cfg;
    unsigned int ddrc_cfg_num;
    struct ddrc_cfg_param *mr_cfg;
    unsigned int mr_cfg_num;
    unsigned int bypass;
};

struct dram_fsp_msg
{
    unsigned int drate;
    bool ssc;
    enum fw_type fw_type;
    /* pstate ddrphy config */
    struct ddrphy_cfg_param *fsp_phy_cfg;
    unsigned int fsp_phy_cfg_num;
    /* pstate message block(header) */
    struct ddrphy_cfg_param *fsp_phy_msgh_cfg;
    unsigned int fsp_phy_msgh_cfg_num;
    /* pstate PIE */
    struct ddrphy_cfg_param *fsp_phy_pie_cfg;
    unsigned int fsp_phy_pie_cfg_num;

    /* for simulation */
    struct ddrphy_cfg_param *fsp_phy_prog_csr_ps_cfg;
    unsigned int fsp_phy_prog_csr_ps_cfg_num;
};

struct dram_timing_info
{
    /* ddrc config */
    struct ddrc_cfg_param *ddrc_cfg;
    unsigned int ddrc_cfg_num;
    /* ddrc pstate config */
    struct dram_fsp_cfg *fsp_cfg;
    unsigned int fsp_cfg_num;
    /* ddrphy config */
    struct ddrphy_cfg_param *ddrphy_cfg;
    unsigned int ddrphy_cfg_num;
    /* ddr fsp train info */
    struct dram_fsp_msg *fsp_msg;
    unsigned int fsp_msg_num;
    /* ddr phy trained CSR */
    struct ddrphy_cfg_param *ddrphy_trained_csr;
    unsigned int ddrphy_trained_csr_num;
    /* ddr phy common PIE */
    struct ddrphy_cfg_param *ddrphy_pie;
    unsigned int ddrphy_pie_num;
    /* initialized drate table */
    unsigned int fsp_table[4];

    /* for emulation */
    unsigned int skip_fw;
    unsigned int prog_csr;
    struct ddrphy_cfg_param *ddrphy_prog_csr;
    unsigned int ddrphy_prog_csr_num;
};

#endif /* __DDR_H */
