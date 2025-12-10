/* Include left configuration with renamed symbols */
#define dram_timing dram_timing_left
#define ddr_ddrc_cfg ddr_ddrc_cfg_left
#define ddr_dram_fsp0_ddrc_cfg ddr_dram_fsp0_ddrc_cfg_left
#define ddr_ddrphy_cfg ddr_ddrphy_cfg_left
#define ddr_ddrphy_trained_csr ddr_ddrphy_trained_csr_left
#define ddr_phy_fsp0_cfg ddr_phy_fsp0_cfg_left
#define ddr_phy_msgh_fsp0_cfg ddr_phy_msgh_fsp0_cfg_left
#define ddr_phy_pie_fsp0_cfg ddr_phy_pie_fsp0_cfg_left
#define ddr_phy_pie ddr_phy_pie_left
#define ddr_dram_fsp_msg ddr_dram_fsp_msg_left
#define ddr_dram_fsp_cfg ddr_dram_fsp_cfg_left
#include "lpddr5_timing_left.c"
#undef dram_timing
#undef ddr_ddrc_cfg
#undef ddr_dram_fsp0_ddrc_cfg
#undef ddr_ddrphy_cfg
#undef ddr_ddrphy_trained_csr
#undef ddr_phy_fsp0_cfg
#undef ddr_phy_msgh_fsp0_cfg
#undef ddr_phy_pie_fsp0_cfg
#undef ddr_phy_pie
#undef ddr_dram_fsp_msg
#undef ddr_dram_fsp_cfg

/* Include right configuration with renamed symbols */
#define dram_timing dram_timing_right
#define ddr_ddrc_cfg ddr_ddrc_cfg_right
#define ddr_dram_fsp0_ddrc_cfg ddr_dram_fsp0_ddrc_cfg_right
#define ddr_ddrphy_cfg ddr_ddrphy_cfg_right
#define ddr_ddrphy_trained_csr ddr_ddrphy_trained_csr_right
#define ddr_phy_fsp0_cfg ddr_phy_fsp0_cfg_right
#define ddr_phy_msgh_fsp0_cfg ddr_phy_msgh_fsp0_cfg_right
#define ddr_phy_pie_fsp0_cfg ddr_phy_pie_fsp0_cfg_right
#define ddr_phy_pie ddr_phy_pie_right
#define ddr_dram_fsp_msg ddr_dram_fsp_msg_right
#define ddr_dram_fsp_cfg ddr_dram_fsp_cfg_right
#include "lpddr5_timing_right.c"
#undef dram_timing
#undef ddr_ddrc_cfg
#undef ddr_dram_fsp0_ddrc_cfg
#undef ddr_ddrphy_cfg
#undef ddr_ddrphy_trained_csr
#undef ddr_phy_fsp0_cfg
#undef ddr_phy_msgh_fsp0_cfg
#undef ddr_phy_pie_fsp0_cfg
#undef ddr_phy_pie
#undef ddr_dram_fsp_msg
#undef ddr_dram_fsp_cfg
