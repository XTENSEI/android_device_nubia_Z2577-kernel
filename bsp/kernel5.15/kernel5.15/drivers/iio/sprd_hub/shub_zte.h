#ifndef SPRD_SHUB_ZTE_H
#define SPRD_SHUB_ZTE_H

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
#include "zlog_common_base.h"
#endif

#define ZTE_SENSOR_LOG_TAG					"[ZTE_LDD_SENSOR][SPRD_KERNEL][sprd-sensor]"
#define ZTE_SENSOR_LOG_DEBUG(fmt, args...)	{ pr_debug(ZTE_SENSOR_LOG_TAG"[Debug] :%s %d:" fmt, __func__, __LINE__, ##args); }
#define ZTE_SENSOR_LOG_INFO(fmt, args...)	{ pr_info(ZTE_SENSOR_LOG_TAG"[Info] :%s %d:" fmt, __func__, __LINE__, ##args); }
#define ZTE_SENSOR_LOG_ERROR(fmt, args...)	{ pr_err(ZTE_SENSOR_LOG_TAG"[Error] :%s %d:" fmt, __func__, __LINE__, ##args); }

struct shub_zte_data {
	void (*cali_callback)(struct shub_zte_data *zdata, u8 *buff, u32 len);
	wait_queue_head_t sensorhub_wq;
	bool sensorhub_init_ready;

	wait_queue_head_t sensor_selftest_wq;
	bool sensor_selftest_over;
	int sensor_selftest_res;

	int sensorhub_assert_times;
};

struct proximity_factory_cali_data {
	/* ground_noise -- auto cali result para */
	uint32_t ground_noise;
	/* high_threshold -- manua cali 3cm result para */
	uint32_t high_threshold;
	/* low_threshold -- manua cali 5cm result para */
	uint32_t low_threshold;
	/* cali_flag -- calibration flag
	 * bit0: 1-auto cali, 0-no auto cali executed
	 * bit1: 1-manual cali 3cm, 0-no manual cali 3cm executed
	 * bit2: 1-manual cali 5cm, 0-no manual cali 5cm executed
	 */
	uint8_t cali_flag;

	/* zte real_ground_noise flag -- real prox cali data */
	uint32_t real_ground_noise;
	/* zte cali_ground_noise -- real ground_noise, to replace ground_noise */
	uint32_t cali_ground_noise;
	/* zte prox cali thresholds set */
	uint16_t cali_min_noise;
	uint16_t cali_max_noise;
};

enum shub_zte_subtype_id {
	SHUB_SET_PROX_CALI_BASE_DATA_SUBTYPE = 0xf0,

	SHUB_SEND_SENSORHUB_INIT_READY_SUBTYPE = 0xf1,

	SHUB_ZLOG_I2C_RW_ERROR_SUBTYPE = 0xf2,
	SHUB_ZLOG_SPI_RW_ERROR_SUBTYPE = 0xf3,
	SHUB_ZLOG_ESD_CHECK_TRI_SUBTYPE = 0xf4,
	SHUB_ZLOG_FACTORY_CALI_SUBTYPE = 0xf5,
	SHUB_ZLOG_STEP_COUNTER_DEC_NO = 0xf6,

	SHUB_SENSOR_SELF_TEST_SUBTYPE = 0xf7,
	SHUB_ZTE_SET_CALI_THRESHOLDS_SUBTYPE = 0xf8,
};

enum shub_rw_type {
	SHUB_READNWU = 1,
	SHUB_READ,
	SHUB_WRITE,
};

void shub_zte_sensor_report_rw_timeout(struct shub_zte_data *zdata, int rw_type);

void shub_zte_probe(struct shub_zte_data *zdata);
void shub_zte_remove(void);

void zte_calibrator_data_store(int sensor_type, u8 *data);
int shub_zte_save_cali_data(struct shub_zte_data *zdata, int sensor_type, u8 *data);
void zte_sensorlist_show(struct shub_zte_data *zdata, u32 number);

void shub_zte_get_data(struct cmd_data *packet);

void shub_zte_sensor_report_assert(struct shub_zte_data *zdata);

#endif
