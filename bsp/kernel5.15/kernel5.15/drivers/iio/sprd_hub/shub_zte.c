#include "shub_core.h"
#include "shub_zte.h"

static int als_cali_coef = CALIB_STATUS_FAIL;
static int ps_cali_ct = 0;
static char alsps_info[10] = {0};

extern int shub_send_event_to_iio(struct shub_data *sensor, u8 *data, u16 len);
extern int shub_send_command(struct shub_data *sensor, int sensor_ID,
				int opcode, const char *data, int len);

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
struct zlog_mod_info zlog_sprd_sensorhub_dev = {
	.module_no = ZLOG_MODULE_SENSOR,
	.name = "sensor",
	.device_name = "sprd_sensorhub",
	.ic_name = "SPRD",
	.module_name = "SENSOR",
	.fops = NULL,
};
struct zlog_client *zlog_sprd_sensorhub_client = NULL;

void shub_zte_sensor_register_zlog_client(void)
{
	zlog_sprd_sensorhub_client = zlog_register_client(&zlog_sprd_sensorhub_dev);
	if (!zlog_sprd_sensorhub_client)
		ZTE_SENSOR_LOG_ERROR("zlog register client zlog_sprd_sensorhub_dev failed\n");
}

void shub_zte_sensor_unregister_zlog_client(void)
{
	if (zlog_sprd_sensorhub_client != NULL)
		zlog_unregister_client(zlog_sprd_sensorhub_client);
}

static void shub_zte_sensor_report_sensor_cali_data(int sensor_type, u8 *data)
{
	int cali_data[6] = {0};
	struct proximity_factory_cali_data prox_cali_data;

	memcpy(cali_data, data, sizeof(cali_data));

	switch (sensor_type) {
	case SENSOR_ACCELEROMETER:
		zlog_client_record(zlog_sprd_sensorhub_client, "acc cali data: %d %d %d\n",
			cali_data[0], cali_data[1], cali_data[2]);
		ZTE_SENSOR_LOG_INFO("'acc cali data: %d %d %d' send to zlog\n",
			cali_data[0], cali_data[1], cali_data[2]);
	break;
	case SENSOR_GYROSCOPE:
		zlog_client_record(zlog_sprd_sensorhub_client, "gyro cali data: %d %d %d %d %d %d\n",
			cali_data[0], cali_data[1], cali_data[2]);
		ZTE_SENSOR_LOG_INFO("'gyro cali data: %d %d %d %d %d %d' send to zlog\n",
			cali_data[0], cali_data[1], cali_data[2], cali_data[3], cali_data[4], cali_data[5]);
	break;
	case SENSOR_LIGHT:
		zlog_client_record(zlog_sprd_sensorhub_client, "als cali data: %d\n", cali_data[0]);
		ZTE_SENSOR_LOG_INFO("'als cali data: %d' send to zlog\n", cali_data[0]);
	break;
	case SENSOR_PROXIMITY:
		memcpy(&prox_cali_data, data, sizeof(struct proximity_factory_cali_data));
		zlog_client_record(zlog_sprd_sensorhub_client, "prox cali data: %d %d %d\n",
				prox_cali_data.ground_noise,
				prox_cali_data.cali_ground_noise,
				prox_cali_data.real_ground_noise);
		ZTE_SENSOR_LOG_INFO("'prox cali data: %d %d %d %d %d' send to zlog\n",
				prox_cali_data.ground_noise,
				prox_cali_data.cali_ground_noise,
				prox_cali_data.real_ground_noise,
				prox_cali_data.cali_min_noise,
				prox_cali_data.cali_max_noise);
	break;
	default:
		ZTE_SENSOR_LOG_INFO("Unrecognized sensor_type %d\n", sensor_type);
	break;
	}

	zlog_client_notify(zlog_sprd_sensorhub_client, ZLOG_SENSOR_FACTORY_CALI_NO);
}

void shub_zte_sensor_report_rw_timeout(struct shub_zte_data *zdata, int rw_type)
{
	static int rdnwu_zlog_times = 0;
	static int rd_zlog_times = 0;
	static int wr_zlog_times = 0;

	if (zdata->sensorhub_init_ready != true) {
		//ZTE_SENSOR_LOG_INFO("sensorhub_init_ready false, rw_type %d return.\n", rw_type);
		return;
	}

	switch (rw_type) {
	case SHUB_READNWU:
		zlog_client_record(zlog_sprd_sensorhub_client, "sprd sensorhub readnwu timeout %d\n", rdnwu_zlog_times);
		rdnwu_zlog_times++;
		ZTE_SENSOR_LOG_INFO("'sprd sensorhub readnwu timeout %d' send to zlog\n", rdnwu_zlog_times);
	break;
	case SHUB_READ:
		zlog_client_record(zlog_sprd_sensorhub_client, "sprd sensorhub read timeout %d\n", rd_zlog_times);
		rd_zlog_times++;
		ZTE_SENSOR_LOG_INFO("'sprd sensorhub read timeout %d' send to zlog\n", rd_zlog_times);
	break;
	case SHUB_WRITE:
		zlog_client_record(zlog_sprd_sensorhub_client, "sprd sensorhub read timeout %d\n", wr_zlog_times);
		wr_zlog_times++;
		ZTE_SENSOR_LOG_INFO("'sprd sensorhub write timeout %d' send to zlog\n", wr_zlog_times);
	break;
	default:
		ZTE_SENSOR_LOG_INFO("Unrecognized rw_type %d\n", rw_type);
	break;
	}

	zlog_client_notify(zlog_sprd_sensorhub_client, ZLOG_SENSOR_SENSORHUB_RW_ERROR_NO);
}

void shub_zte_sensor_report_assert(struct shub_zte_data *zdata)
{
	ZTE_SENSOR_LOG_INFO("'sprd sensorhub assert %d' send to zlog\n", zdata->sensorhub_assert_times);
	zlog_client_record(zlog_sprd_sensorhub_client, "sprd sensorhub assert %d\n", zdata->sensorhub_assert_times);
	zlog_client_notify(zlog_sprd_sensorhub_client, ZLOG_SENSOR_SENSORHUB_ASSERT_NO);
	zdata->sensorhub_assert_times++;
}

#else
void shub_zte_sensor_register_zlog_client(void)
{
}

void shub_zte_sensor_unregister_zlog_client(void)
{
}

static void shub_zte_sensor_report_sensor_cali_data(int sensor_type, u8 *data)
{
}

void shub_zte_sensor_report_rw_timeout(struct shub_zte_data *zdata, int rw_type)
{
}

void shub_zte_sensor_report_assert(struct shub_zte_data *zdata)
{
}
#endif

static ssize_t als_cali_coef_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 10, "%d\n", als_cali_coef);
}
static DEVICE_ATTR_RO(als_cali_coef);

static ssize_t alsps_info_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return snprintf(buf, sizeof(alsps_info), "%s\n", alsps_info);
}
static DEVICE_ATTR_RO(alsps_info);

static ssize_t ps_cali_ct_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 10, "%d\n", ps_cali_ct);
}
static DEVICE_ATTR_RO(ps_cali_ct);

static ssize_t sensor_selftest_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	u8 buffer[6];
	int ret, sensor_handle;
	struct shub_data *sensor = dev_get_drvdata(dev);

	memcpy(buffer, buf, sizeof(buffer));

	ret = sscanf(buffer, "%d", &sensor_handle);
	if (ret > 0)
		ZTE_SENSOR_LOG_INFO("sensor_selftest sensor_handle = %d\n", sensor_handle);

	sensor->shub_zdata.sensor_selftest_over = false;
	sensor->shub_zdata.sensor_selftest_res = 0;

	shub_send_command(sensor, sensor_handle,
				SHUB_SENSOR_SELF_TEST_SUBTYPE, buffer,
				sizeof(buffer));

	return count;
}

static ssize_t sensor_selftest_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct shub_data *sensor = dev_get_drvdata(dev);
	int ret;

	wait_event_interruptible_timeout(sensor->shub_zdata.sensor_selftest_wq,
						(sensor->shub_zdata.sensor_selftest_over),
						msecs_to_jiffies
						(RECEIVE_TIMEOUT_MS));

	if (sensor->shub_zdata.sensor_selftest_res == 1)
		ret = 0;
	else
		ret = -1;

	return sprintf(buf, "%d\n", ret);
}
static DEVICE_ATTR_RW(sensor_selftest);

static ssize_t set_cali_thresholds_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct shub_data *sensor = dev_get_drvdata(dev);
	int sensor_type;
	int cali_thresholds[2];

	ZTE_SENSOR_LOG_INFO("buf=%s\n", buf);

	if (sscanf(buf, "%d %d %d", &sensor_type, &cali_thresholds[0], &cali_thresholds[1]) != 3)
		return -EINVAL;

	ZTE_SENSOR_LOG_INFO("sensor_type = %d min_noise = %d, max_noise = %d\n",
			sensor_type, cali_thresholds[0], cali_thresholds[1]);

	if ((cali_thresholds[0] < 0) || (cali_thresholds[0] > 0xFFFF) ||
			(cali_thresholds[1] < 0) || (cali_thresholds[1] > 0xFFFF) ||
			(cali_thresholds[0] >= cali_thresholds[1])) {
		ZTE_SENSOR_LOG_INFO("cali_thresholds invalid\n");
		return -EINVAL;
	}

	if (shub_send_command(sensor, sensor_type,
				SHUB_ZTE_SET_CALI_THRESHOLDS_SUBTYPE,
				(char *)cali_thresholds, sizeof(cali_thresholds)) < 0) {
		ZTE_SENSOR_LOG_ERROR("shub_send_commond Fail\n");
		return -EINVAL;
	}

	return count;
}
static DEVICE_ATTR_WO(set_cali_thresholds);

static struct attribute *sensorhub_zte_attrs[] = {
	&dev_attr_als_cali_coef.attr,
	&dev_attr_ps_cali_ct.attr,
	&dev_attr_alsps_info.attr,
	&dev_attr_sensor_selftest.attr,
	&dev_attr_set_cali_thresholds.attr,
	NULL,
};
ATTRIBUTE_GROUPS(sensorhub_zte);

static void shub_cali_callback(struct shub_zte_data *zdata, u8 *data, u32 len)
{
	int sensor_type = SENSOR_PROXIMITY;
	struct proximity_factory_cali_data prox_cali_data;
	struct shub_data *sensor = container_of(zdata, struct shub_data, shub_zdata);

	memcpy(&prox_cali_data, data, sizeof(struct proximity_factory_cali_data));

	sensor->cali_store.cmd = HAL_CALI_STORE;
	sensor->cali_store.length = CALIBRATION_DATA_LENGTH;
	sensor->cali_store.type = sensor_type;
	memcpy(sensor->cali_store.udata, data, CALIBRATION_DATA_LENGTH);

	shub_zte_sensor_report_sensor_cali_data(sensor_type, data);

	ZTE_SENSOR_LOG_INFO("sensor_type = %d ground_noise=%d real_ground_noise=%d cali_ground_noise=%d cali_min/max_noise=%d %d\n",
			sensor_type,
			prox_cali_data.ground_noise,
			prox_cali_data.real_ground_noise,
			prox_cali_data.cali_ground_noise,
			prox_cali_data.cali_min_noise, prox_cali_data.cali_max_noise);

	shub_send_event_to_iio(sensor, (u8 *)&sensor->cali_store, sizeof(sensor->cali_store));
}

void shub_zte_probe(struct shub_zte_data *zdata)
{
	int ret;
	struct shub_data *mcu = container_of(zdata, struct shub_data, shub_zdata);

	mcu->shub_zdata.cali_callback = shub_cali_callback;

	init_waitqueue_head(&mcu->shub_zdata.sensorhub_wq);
	mcu->shub_zdata.sensorhub_init_ready = false;

	init_waitqueue_head(&mcu->shub_zdata.sensor_selftest_wq);
	mcu->shub_zdata.sensor_selftest_over = false;

	shub_zte_sensor_register_zlog_client();

	ret = sysfs_create_groups(&mcu->sensor_dev->kobj, sensorhub_zte_groups);
	if (ret)
		ZTE_SENSOR_LOG_ERROR("failed to create zte sysfs device attributes\n");

	mcu->shub_zdata.sensorhub_assert_times = 0;
}

void shub_zte_remove(void)
{
	shub_zte_sensor_unregister_zlog_client();
}

void zte_calibrator_data_store(int sensor_type, u8 *data)
{
	int cali_data[3] = {0};

	memcpy(cali_data, data, sizeof(cali_data));

	ZTE_SENSOR_LOG_INFO("sensor_type = %d cali_data = %d %d %d send to sensorhub\n",
			sensor_type, cali_data[0], cali_data[1], cali_data[2]);

	if (sensor_type == SENSOR_LIGHT) {
		als_cali_coef = cali_data[0];
		ZTE_SENSOR_LOG_INFO("als_cali_coef init to %d\n", als_cali_coef);
	}

	if (sensor_type == SENSOR_PROXIMITY) {
		struct proximity_factory_cali_data prox_cali_data;

		memcpy(&prox_cali_data, data, sizeof(struct proximity_factory_cali_data));
		ZTE_SENSOR_LOG_INFO("prox_cali_data ground_noise:%d cali_ground_noise:%d real_ground_noise:%d cali_min/max_noise:%d %d\n",
				prox_cali_data.ground_noise,
				prox_cali_data.cali_ground_noise,
				prox_cali_data.real_ground_noise,
				prox_cali_data.cali_min_noise,
				prox_cali_data.cali_max_noise);
		ps_cali_ct = (prox_cali_data.real_ground_noise > 0) ?
				prox_cali_data.real_ground_noise : prox_cali_data.ground_noise;
		ZTE_SENSOR_LOG_INFO("ps_cali_ct init to %d\n", ps_cali_ct);
	}
}

int shub_zte_save_cali_data(struct shub_zte_data *zdata,
					int sensor_type, u8 *data)
{
	int err;
	int cali_data[3] = {0};
	struct shub_data *sensor = container_of(zdata, struct shub_data, shub_zdata);

	memcpy(cali_data, data, sizeof(cali_data));

	sensor->cali_store.cmd = HAL_CALI_STORE;
	sensor->cali_store.length = CALIBRATION_DATA_LENGTH;
	sensor->cali_store.type = sensor_type;
	memcpy(sensor->cali_store.udata, data, CALIBRATION_DATA_LENGTH);

	ZTE_SENSOR_LOG_INFO("sensor_type = %d sensor->cali_data = [%d %d %d]\n",
			sensor_type, cali_data[0], cali_data[1], cali_data[2]);

	err = shub_send_event_to_iio(sensor, (u8 *)&sensor->cali_store, sizeof(sensor->cali_store));

	shub_zte_sensor_report_sensor_cali_data(sensor_type, data);

	if (sensor_type == SENSOR_LIGHT) {
		als_cali_coef = cali_data[0];
		ZTE_SENSOR_LOG_INFO("als_cali_coef set to %d\n", als_cali_coef);
	}

	if (sensor_type == SENSOR_PROXIMITY) {
		struct proximity_factory_cali_data prox_cali_data;

		memcpy(&prox_cali_data, data, sizeof(struct proximity_factory_cali_data));
		ZTE_SENSOR_LOG_INFO("sensor_type = %d prox_cali_data = %d %d %d\n",
				sensor_type,
				prox_cali_data.ground_noise,
				prox_cali_data.cali_ground_noise,
				prox_cali_data.real_ground_noise);

		ps_cali_ct = (prox_cali_data.real_ground_noise > 0) ?
				prox_cali_data.real_ground_noise : prox_cali_data.ground_noise;
		ZTE_SENSOR_LOG_INFO("ps_cali_ct set to %d\n", ps_cali_ct);

		if (prox_cali_data.real_ground_noise != prox_cali_data.ground_noise) {
			ZTE_SENSOR_LOG_INFO("prox real_ground_noise %d, ground_noise %d prox cali failed\n",
				prox_cali_data.real_ground_noise, prox_cali_data.ground_noise);
			return -1;
		}
	}

	return err;
}

void zte_sensorlist_show(struct shub_zte_data *zdata, u32 number)
{
	int i;
	struct shub_data *sensor = container_of(zdata, struct shub_data, shub_zdata);

	for (i = 0; i < number; i++) {
		ZTE_SENSOR_LOG_INFO("sensor_info_list[%d]: name:%s vendor:%s maxdelay_us:%d mindelay_us:%d version:%d\n",
			i, sensor->sensor_info_list[i].name,
			sensor->sensor_info_list[i].vendor,
			sensor->sensor_info_list[i].maxdelay_us,
			sensor->sensor_info_list[i].mindelay_us,
			sensor->sensor_info_list[i].version);
		if ((strncmp(sensor->sensor_info_list[i].name, "prox_", 5)) == 0)
			strcpy(alsps_info, sensor->sensor_info_list[i].name + 5);
	}
}

void shub_zte_get_data(struct cmd_data *packet)
{
	switch (packet->subtype) {
	case SHUB_SET_PROX_CALI_BASE_DATA_SUBTYPE:
		g_sensor->shub_zdata.cali_callback(&g_sensor->shub_zdata, packet->buff,
					packet->length);
		break;

	case SHUB_SEND_SENSORHUB_INIT_READY_SUBTYPE:
		ZTE_SENSOR_LOG_INFO("sensorhub init ready!\n");
		g_sensor->shub_zdata.sensorhub_init_ready = true;
		wake_up_interruptible(&g_sensor->shub_zdata.sensorhub_wq);
		break;

	case SHUB_SENSOR_SELF_TEST_SUBTYPE:
		memcpy(&g_sensor->shub_zdata.sensor_selftest_res, packet->buff, sizeof(g_sensor->shub_zdata.sensor_selftest_res));
		g_sensor->shub_zdata.sensor_selftest_over = true;
		wake_up_interruptible(&g_sensor->shub_zdata.sensor_selftest_wq);
		ZTE_SENSOR_LOG_INFO("sensor self_test %d\n", g_sensor->shub_zdata.sensor_selftest_res);
		break;

	default:
		break;
	}

#ifdef CONFIG_VENDOR_ZTE_LOG_EXCEPTION
	if (g_sensor->shub_zdata.sensorhub_init_ready == true) {
		int zlog_type = 0;

		switch (packet->subtype) {
		case SHUB_ZLOG_I2C_RW_ERROR_SUBTYPE:
			zlog_type = ZLOG_SENSOR_I2C_RW_ERROR_NO;
			break;
		case SHUB_ZLOG_SPI_RW_ERROR_SUBTYPE:
			zlog_type = ZLOG_SENSOR_SPI_RW_ERROR_NO;
			break;
		case SHUB_ZLOG_ESD_CHECK_TRI_SUBTYPE:
			zlog_type = ZLOG_SENSOR_ESD_CHECK_TRI_NO;
			break;
		case SHUB_ZLOG_FACTORY_CALI_SUBTYPE:
			zlog_type = ZLOG_SENSOR_FACTORY_CALI_NO;
			break;
		case SHUB_ZLOG_STEP_COUNTER_DEC_NO:
			zlog_type = ZLOG_SENSOR_STEP_COUNTER_DEC_NO;
			break;
		default:
			zlog_type = 0;
			break;
		}

		if (zlog_type > 0) {
			zlog_client_record(zlog_sprd_sensorhub_client, "%s\n", packet->buff);
			zlog_client_notify(zlog_sprd_sensorhub_client, zlog_type);
			ZTE_SENSOR_LOG_INFO("type=%d data:%s reported to zlog\n",
					zlog_type, packet->buff);
		}
	}
#endif
}
