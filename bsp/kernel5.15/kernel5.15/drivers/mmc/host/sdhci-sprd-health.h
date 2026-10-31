/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SDHCI_SPRD_HEALTH_H
#define _SDHCI_SPRD_HEALTH_H

#define LITTLE_ENDIAN 0

#define TOTAL_HEALTH_BYTE 512

#define ZTE_HEALTH_INFO
#ifndef ZTE_HEALTH_INFO
#define HEALTH_FILED_NUM 19
#else
#define HEALTH_FILED_NUM 41  //max ,19
#endif

#define PROC_MODE 0440
#define HEALTH_CMD_ARG1 0x4B534BFB
#define HEALTH_CMD_ARG2 0x0D

#ifdef ZTE_HEALTH_INFO
//ZTE ADD
#define PHISON_HEALTH_CMD_ARG1 0x484e58fb
#define PHISON_HEALTH_CMD_ARG2 0x00000035
#define PHISION_HEALTH_FILED_NUM 41

#define VENDOR_CMD_60 60
#define BIWIN_HEALTH_CMD_ARG1 0x534d4900
#define BIWIN_HEALTH_CMD_ARG2 0x52451111
#define BIWIN_HEALTH_CMD_ARG3 0x1
#define BIWIN_HEALTH_CMD_ARG4 0xF0000061
#define BIWIN_HEALTH_FILED_NUM 29

#define SCY_HEALTH_CMD_ARG1 0xF0000081
#define SCY_HEALTH_FILED_NUM 24

#define RAYSON_HEALTH_CMD_ARG1 0x000005F1
#define RAYSON_HEALTH_FILED_NUM 21

//#define YEESTOR_HEALTH_CMD_ARG1 0x300005F0  WRITE
#define YEESTOR_HEALTH_CMD_ARG1 0x300005F1
#define YEESTOR_HEALTH_FILED_NUM 27

#define LONGSYS_HEALTH_CMD_ARG1 0x110005F1
#define LONGSYS_HEALTH_FILED_NUM 21

//#define MMC_GEN_CMD			(56)

static const u16 phision_health_offset[] = {
	0, 4, 12, 16, 20, 24, 28, 32, 36, 40,
	44, 48, 52, 56, 60, 64, 68, 92, 96, 100,
	104, 108, 112, 116, 120, 124, 128, 132, 136, 140,
	144, 152, 156, 160, 164, 168, 172, 184, 188, 248, 
	452,
};

static const u16 phision_health_size[] = {
	4,	4,	4,	4,	4,	4,	4,	4,	4,	4,
	4,	4,	4,	4,	4,	4,	4,	4,	4,	4,
	4,	4,	4,	4,	4,	4,	4,	4,	4,	4,
	4,	4,	4,	4,	4,	4,	4,	4,	4,	8,
	4,
};

static const bool phision_health_parsing_type[] = {
	true,	true,	true,	true,	true,	true,	true,	true,	true,	true,
	true,	true,	true,	true,	true,	true,	true,	true,	true,	true,
	true,	true,	true,	true,	true,	true,	true,	true,	true,	true,
	true,	true,	true,	true,	true,	true,	true,	true,	true,	true,
	false,
};

static const char *const phision_health_name[] = {
	"original_bad_count",
	"reserve_block_for_rtbb",
	"rtbb_tlc",
	"rtbb_slc",
	"rtbb_esf",
	"rtbb_psf",
	"rtbb_uecc",
	"read_uecc_count",
	"hecc_count",
	"metadata_corruption",

	"read_reclaim_slc",
	"read_reclaim_tlc",
	"vdt_vccq_count",
	"vdt_vcc_count",
	"spor_count",
	"spor_recovery_succ_count",
	"spor_recovery_fail_count",
	"slc_min_erase_count",
	"slc_max_erase_count",
	"slc_avg_erase_count",

	"tlc_min_erase_count",
	"tlc_max_erase_count",
	"tlc_avg_erase_count",
	"host_data_read",
	"host_data_write",
	"poweron_count",
	"WAF_milli",
	"cur_temperature_for_ce11",
	"cur_temperature_for_ce12",
	"cur_temperature_for_ce13",

	"cur_temperature_for_ce14",
	"data_block_reclaim_count",
	"table_block_reclaim_count",
	"slc_buffer_size",
	"tlc_buffer_size",
	"history_min_nand_temparature",
	"history_max_nand_temparature",
	"ffu_succ_count",
	"ffu_fail_count",
	"internal_trace_number",

	"sram_fail_count"

};

static const u16 biwin_health_offset[] = {
	0,	8,	16,	20,	24,	28,	32,	36,	40,	44,
	48,	50,	52,	56,	60,	64,	68,	72,	76,	80,
	84,	88,	92,	96,	97,	98,	99,	100,	104,
};

static const u16 biwin_health_size[] = {
	8,	8,	4,	4,	4,	4,	4,	4,	2,	1,
	2,	2,	4,	4,	4,	4,	4,	4,	4,	4,
	4,	4,	4,	1,	1,	1,	1,	4,	4,
//	4,	4,	2,	4,	2,	2,	2,	2,	4,
};

static const bool biwin_health_parsing_type[] = {
	false,	false,	false,	false,	false,	false,	false,	false,	false,	false,
	false,	false,	false,	false,	false,	false,	false,	false,	false,	false,
	false,	false,	false,	false,	false,	false,	false,	false,	false,
};

static const char *const biwin_health_name[] = {
	"host_data_read",
	"host_data_write",
	"slc_min_erase_count",
	"slc_max_erase_count",
	"slc_avg_erase_count",
	"tlc_min_erase_count",
	"tlc_max_erase_count",
	"tlc_avg_erase_count",
	"original_bad_count",
	"rtbb_count",

	"euda_spare_block",
	"uda_spare_block",
	"metadata_corruption",
	"poweron_count",
	"read_uecc_count",
	"slc_reserve_block",
	"tlc_reserve_block",
	"read_retry_count",
	"table_block_reclaim_count",
	"data_block_reclaim_count",

	"raw_card_capacity",
	"vdt_count",
	"spor_count",
	"ext_csd268",
	"ext_csd269",
	"manuf_id",
	"manuf_date",
	"product_name",
	"ext_csd267",

};
static const u16 scy_health_offset[] = {
	0,	4,	8,	10,	12,	16,	20,	24,	28,	32,
	36,	40,	44,	48,	52,	56,	60,	64,	68,	72,
	76,	80,	84,	88,
};

static const u16 scy_health_size[] = {
	4,	4,	2,	2,	4,	4,	4,	4,	4,	4,
	4,	4,	4,	4,	4,	4,	4,	4,	4,	4,
	4,	4,	4,	4,
};

static const bool scy_health_parsing_type[] = {
	false,	false,	false,	false,	false,	false,	false,	false,	false,	false,
	false,	false,	false,	false,	false,	false,	false,	false,	false,	false,
	false,	false,	false,	false,	
};

static const char *const scy_health_name[] = {
	"original_bad_count",
	"rtbb_count",
	"slc_spare_block",
	"tlc_spare_block",
	"uecc_count",
	"host_data_write_100M",   //*100m
	"host_data_read_100M",      //*100m
	"spor_count",
	"vdt_vcc_count",
	"metadata_corruption",

	"slc_min_erase_count",
	"slc_max_erase_count",
	"slc_avg_erase_count",
	"tlc_min_erase_count",
	"tlc_max_erase_count",
	"tlc_avg_erase_count",
	"ffu_count",
	"cumulative_init_count",
	"slc_reclaim_count",
	"tlc_reclaim_count",
	
	"rtbb_slc",
	"rtbb_tlc",
	"write_crc",
	"cmd_crc",
};


static const u16 rayson_health_offset[] = {
	0,	8,	16,	18,	20,	22,	24,	26,	28,	30,
	32,	34,	36,	40,	48,	52,	64,	68,	74,	75,
	76,
};

static const u16 rayson_health_size[] = {
	8,	8,	2,	2,	2,	2,	2,	2,	2,	2,
	2,	2,	4,	4,	4,	4,	4,	4,	1,	1,
	1,
};

static const bool rayson_health_parsing_type[] = {
	false,	false,	false,	false,	false,	false,	false,	false,	false,	false,
	false,	false,	false,	false,	false,	false,	false,	false,	false,	false,
	false,	
};

static const char *const rayson_health_name[] = {
	"host_data_write_sector",   //*sector 
	"host_data_read_sector",      //*sector 
	"slc_min_erase_count",
	"slc_max_erase_count",
	"slc_avg_erase_count",
	"tlc_min_erase_count",
	"tlc_max_erase_count",
	"tlc_avg_erase_count",
	
	"rtbb_count",
	"original_bad_count",   //30-31

	"reserve_block_for_slc",
	"reserve_block_for_tlc",
	"slc_reclaim_count",
	"tlc_reclaim_count",
	
	//"slc_spare_block",
	//"tlc_spare_block",
	"cumulative_init_count",
	"spor_count",
	"uecc_count",
	"read_retry_count",

	"ext_csd268",
	"ext_csd269",
	"ext_csd267",
	
};

static const u16 yeestor_health_offset[] = {
	1,	2,	4,	6,	8,	12,	16,	20,	24,	32,
	36,	40,	44,	48,	52,	56,	60,	62,	64,	68,
	72,	76,	80,	84,	88,	92,	96,	
};

static const u16 yeestor_health_size[] = {
	1,	2,	2,	2,	4,	4,	4,	4,	8,	4,
	4,	4,	4,	4,	4,	4,	2,	2,	4,	4,
	4,	4,	4,	4,	4,	4,	4,	
};

//[15-12] false  
static const bool yeestor_health_parsing_type[] = {
	false,	false,	false,	false,	false,	false,	false,	false,	false,	false,
	false,	false,	false,	false,	false,	false,	false,	false,	false,	false,
	false,	false,	false,	false,	false,	false,	false,
};

static const char *const yeestor_health_name[] = {
	
	"slc_boot_status",
	"HCGC",
	"HCGC_delay_ms",
	"HCGC_status",
	"slc_free_limit_MB",  //MB
	"slc_free_space_MB",  //MB
	"tlc_free_space_MB",  //MB
	"valid_data_MB",  //MB
	
	"host_data_write_sector",   //*sector 

	"slc_pe",
	"tlc_pe",
	"por_count",
	"write_crc",
	"cmd_crc",
	"vdt_count",
	"read_retry_count",
	"rtbb_slc",
	"rtbb_tlc",
	"tlc_min_erase_count",
	"tlc_max_erase_count",
	"tlc_total_erase_count",
	"tlc_avg_erase_count",
	"slc_min_erase_count",
	"slc_max_erase_count",
	"slc_total_erase_count",
	"slc_avg_erase_count",
	"ffu_count",
	
};


static const u16 longsys_health_offset[] = {
	0,	25,	27,	32,	36,	40,	44,	48,	52,	56,
	60,	64,	71,	72,	76,	86,	96,	100,	112,	120,
	128
};

static const u16 longsys_health_size[] = {
	16,	2,	1,	4,	4,	4,	4,	4,	4,	4,
	4,	4,	1,	4,	4,	1,	4,	4,	8,	8,
	6,
};

//[15-12] false  
static const bool longsys_health_parsing_type[] = {
	false,	false,	false,	false,	false,	false,	false,	false,	false,	false,
	false,	false,	false,	false,	false,	false,	false,	false,	false,	false,
	false,	
};

static const char *const longsys_health_name[] = {
	
	"flash_ID",

	"original_bad_count",
	"rtbb_count",

	"tlc_min_erase_count",
	"tlc_max_erase_count",
	"tlc_total_erase_count",
	"tlc_avg_erase_count",
	"slc_min_erase_count",
	"slc_max_erase_count",
	"slc_total_erase_count",
	"slc_avg_erase_count",
	"raw_card_capacity",

	"ffu_succ_count",
	"crc_count",
	"poweron_count",

	"manuf_id", 

	"tlc_reclaim_count",
	"high_precision_avg_erase_count",
  
	"host_data_write_128M",   //*128M
	"host_data_read_128M",   //*128M
	"fw_version", 
	
};
#endif

int sprd_mmc_health_init(struct mmc_card *card);

#endif
