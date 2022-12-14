/* BOSCH Gyroscope Sensor Driver
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * History: V1.0 --- [2013.01.29]Driver creation
 *          V1.1 --- [2013.03.28]
 *                   1.Instead late_resume, use resume to make sure
 *                     driver resume is ealier than processes resume.
 *                   2.Add power mode setting in read data.
 *          V1.2 --- [2013.06.28]Add self test function.
 *          V1.3 --- [2013.07.26]Fix the bug of wrong axis remapping
 *                   in rawdata inode.
 */

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/module.h>

#include <gyroscope.h>
#include <cust_gyro.h>
#include <hwmsensor.h>
#include <hwmsen_dev.h>
#include <sensors_io.h>
#include <hwmsen_helper.h>
#include "bmi160_gyro.h"

#define CALIBRATION_TO_FILE
#ifdef CALIBRATION_TO_FILE
#include <linux/syscalls.h>
#include <linux/fs.h>
#endif

#define SUPPORT_DYNAMIC_UPDATE_GYRO_BIAS
#ifdef SUPPORT_DYNAMIC_UPDATE_GYRO_BIAS
struct dynamic_gyro_bias{
	/*dynamic bias valirable*/
	bool dynamic_update_gyro_bias;

	int stable_criteria_x;
	int stable_criteria_y;
	int stable_criteria_z;
	int stable_criteria_count;
	int stable_check_count;
	int gyrodata[BMG_AXES_NUM+1];

	int new_bias_x;
	int new_bias_y;
	int new_bias_z;

	/*update check valirable*/
	int update_criteria_x;
	int update_criteria_y;
	int update_criteria_z;
	int update_criteria_count;
	int update_gyrodata[BMG_AXES_NUM+1]; /*these are only for checking stable or not */
	int is_stable;
};
static struct dynamic_gyro_bias dynamic_gyro;

#define STABLE_DEFAULT_CRITERIA_X (20)
#define STABLE_DEFAULT_CRITERIA_Y (20)
#define STABLE_DEFAULT_CRITERIA_Z (20)
/* Firstly, new bias will be updatd after (STABLE_CRITERIA_COUNT) count on Excutefunc()*/
#define STABLE_DEFAULT_CRITERIA_COUNT (50)

#define UPDATE_DEFAULT_CRITERIA_X (15)
#define UPDATE_DEFAULT_CRITERIA_Y (15)
#define UPDATE_DEFAULT_CRITERIA_Z (10)
/* d_gyro will update new bias after (STABLE_CRITERIA_COUNT + UPDATE_CRITERIA_COUNT) count on Updatefunc()*/
#define UPDATE_DEFAULT_CRITERIA_COUNT (30)

static void bmg_ExcuteDynamicGyroBias(struct dynamic_gyro_bias* d_gyro,	s16* data, s16* cali);
static int bmg_CheckStableToUpdateBias(struct dynamic_gyro_bias* d_gyro, s16* data, int criteria);
static void bmg_UpdateDynamicGyroBias(struct dynamic_gyro_bias* d_gyro, int* data);
#endif

/* sensor type */
enum SENSOR_TYPE_ENUM {
	BMI160_GYRO_TYPE = 0x0,
	INVALID_TYPE = 0xff
};

/* range */
enum BMG_RANGE_ENUM {
	BMG_RANGE_2000 = 0x0,	/* +/- 2000 degree/s */
	BMG_RANGE_1000,			/* +/- 1000 degree/s */
	BMG_RANGE_500,			/* +/- 500 degree/s */
	BMG_RANGE_250,			/* +/- 250 degree/s */
	BMG_RANGE_125,			/* +/- 125 degree/s */
	BMG_UNDEFINED_RANGE = 0xff
};

/* power mode */
enum BMG_POWERMODE_ENUM {
	BMG_SUSPEND_MODE = 0x0,
	BMG_NORMAL_MODE,
	BMG_UNDEFINED_POWERMODE = 0xff
};

/* debug infomation flags */
enum GYRO_TRC {
	GYRO_TRC_FILTER  = 0x01,
	GYRO_TRC_RAWDATA = 0x02,
	GYRO_TRC_IOCTL   = 0x04,
	GYRO_TRC_CALI	= 0x08,
	GYRO_TRC_INFO	= 0x10,
};

/* s/w data filter */
struct data_filter {
	s16 raw[C_MAX_FIR_LENGTH][BMG_AXES_NUM];
	int sum[BMG_AXES_NUM];
	int num;
	int idx;
};

/* bmg i2c client data */
struct bmg_i2c_data {
	struct i2c_client *client;
	struct gyro_hw *hw;
	struct hwmsen_convert   cvt;
	/* sensor info */
	u8 sensor_name[MAX_SENSOR_NAME];
	enum SENSOR_TYPE_ENUM sensor_type;
	enum BMG_POWERMODE_ENUM power_mode;
	enum BMG_RANGE_ENUM range;
	int datarate;
	/* sensitivity = 2^bitnum/range
	[+/-2000 = 4000; +/-1000 = 2000;
	 +/-500 = 1000; +/-250 = 500;
	 +/-125 = 250 ] */
	u16 sensitivity;
	/*misc*/
	struct mutex lock;
	atomic_t	trace;
	atomic_t	suspend;
	atomic_t	filter;
	atomic_t	fast_calib_rslt;
	atomic_t	selftest_result;
	s16	data[BMG_AXES_NUM+1];
	/* unmapped axis value */
	s16	cali_sw[BMG_AXES_NUM+5];
	/* hw offset */
	s8	offset[BMG_AXES_NUM+1];/* +1:for 4-byte alignment */
#ifdef SUPPORT_DYNAMIC_UPDATE_GYRO_BIAS
	s16	cali_data[BMG_AXES_NUM+1];
#endif

#if defined(CONFIG_BMG_LOWPASS)
	atomic_t	firlen;
	atomic_t	fir_en;
	struct data_filter	fir;
#endif
};

struct gyro_hw gyro_cust;
static struct gyro_hw *hw = &gyro_cust;

extern struct i2c_client *bmi160_acc_i2c_client;

static struct gyro_init_info bmi160_gyro_init_info;
/* 0=OK, -1=fail */
static int bmi160_gyro_init_flag =-1;
static struct i2c_driver bmg_i2c_driver;
static struct bmg_i2c_data *obj_i2c_data;
static int bmg_set_powermode(struct i2c_client *client,
		enum BMG_POWERMODE_ENUM power_mode);
static const struct i2c_device_id bmg_i2c_id[] = {
	{BMG_DEV_NAME, 0},
	{}
};

struct bmg_axis_data_t {
	s16 x;
	s16 y;
	s16 z;
};

/* I2C operation functions */
static int bmg_i2c_read_block(struct i2c_client *client, u8 addr,
				u8 *data, u8 len)
{
	int err;
	u8 beg = addr;
	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,	.flags = 0,
			.len = 1,	.buf = &beg
		},
		{
			.addr = client->addr,	.flags = I2C_M_RD,
			.len = len,	.buf = data,
		}
	};
	if (!client)
		return -EINVAL;
	else if (len > C_I2C_FIFO_SIZE) {
		GYRO_ERR("length %d exceeds %d\n", len, C_I2C_FIFO_SIZE);
		return -EINVAL;
	}
	err = i2c_transfer(client->adapter, msgs, sizeof(msgs)/sizeof(msgs[0]));
	if (err != 2) {
		GYRO_ERR("i2c_transfer error: (%d %p %d) %d\n",
			addr, data, len, err);
		err = -EIO;
	} else {
		err = 0;
	}
	return err;
}

static int bmg_i2c_write_block(struct i2c_client *client, u8 addr,
				u8 *data, u8 len)
{
	/*
	*because address also occupies one byte,
	*the maximum length for write is 7 bytes
	*/
	int err = 0;
	int idx = 0;
	int num = 0;
	char buf[C_I2C_FIFO_SIZE];
	if (!client)
		return -EINVAL;
	else if (len >= C_I2C_FIFO_SIZE) {
		GYRO_ERR("length %d exceeds %d\n", len, C_I2C_FIFO_SIZE);
		return -EINVAL;
	}
	buf[num++] = addr;
	for (idx = 0; idx < len; idx++) {
		buf[num++] = data[idx];
	}
	err = i2c_master_send(client, buf, num);
	if (err < 0) {
		GYRO_ERR("send command error.\n");
		return -EFAULT;
	} else {
		err = 0;
	}
	return err;
}

static int bmg_read_raw_data(struct i2c_client *client, s16 data[BMG_AXES_NUM])
{
	int err = 0;
	struct bmg_i2c_data *priv = obj_i2c_data;
	if (priv->power_mode == BMG_SUSPEND_MODE) {
		err = bmg_set_powermode(client,
			(enum BMG_POWERMODE_ENUM)BMG_NORMAL_MODE);
		if (err < 0) {
			GYRO_ERR("set power mode failed, err = %d\n", err);
			return err;
		}
	}
	if (priv->sensor_type == BMI160_GYRO_TYPE) {
		u8 buf_tmp[BMG_DATA_LEN] = {0};
		err = bmg_i2c_read_block(client, BMI160_USER_DATA_8_GYR_X_LSB__REG,
				buf_tmp, 6);
		if (err) {
			GYRO_ERR("read gyro raw data failed.\n");
			return err;
		}
		/* Data X */
		data[BMG_AXIS_X] = (s16)
			((((s32)((s8)buf_tmp[1]))
			  << BMI160_SHIFT_8_POSITION) | (buf_tmp[0]));
		/* Data Y */
		data[BMG_AXIS_Y] = (s16)
			((((s32)((s8)buf_tmp[3]))
			  << BMI160_SHIFT_8_POSITION) | (buf_tmp[2]));
		/* Data Z */
		data[BMG_AXIS_Z] = (s16)
			((((s32)((s8)buf_tmp[5]))
			  << BMI160_SHIFT_8_POSITION) | (buf_tmp[4]));
		if (atomic_read(&priv->trace) & GYRO_TRC_RAWDATA) {
			GYRO_LOG("[%s][16bit raw]"
			"[%08X %08X %08X] => [%5d %5d %5d]\n",
			priv->sensor_name,
			data[BMG_AXIS_X],
			data[BMG_AXIS_Y],
			data[BMG_AXIS_Z],
			data[BMG_AXIS_X],
			data[BMG_AXIS_Y],
			data[BMG_AXIS_Z]);
		}
	}

#ifdef CONFIG_BMG_LOWPASS
/*
*Example: firlen = 16, filter buffer = [0] ... [15],
*when 17th data come, replace [0] with this new data.
*Then, average this filter buffer and report average value to upper layer.
*/
	if (atomic_read(&priv->filter)) {
		if (atomic_read(&priv->fir_en) &&
				 !atomic_read(&priv->suspend)) {
			int idx, firlen = atomic_read(&priv->firlen);
			if (priv->fir.num < firlen) {
				priv->fir.raw[priv->fir.num][BMG_AXIS_X] =
						data[BMG_AXIS_X];
				priv->fir.raw[priv->fir.num][BMG_AXIS_Y] =
						data[BMG_AXIS_Y];
				priv->fir.raw[priv->fir.num][BMG_AXIS_Z] =
						data[BMG_AXIS_Z];
				priv->fir.sum[BMG_AXIS_X] += data[BMG_AXIS_X];
				priv->fir.sum[BMG_AXIS_Y] += data[BMG_AXIS_Y];
				priv->fir.sum[BMG_AXIS_Z] += data[BMG_AXIS_Z];
				if (atomic_read(&priv->trace)&GYRO_TRC_FILTER) {
					GYRO_LOG("add [%2d]"
					"[%5d %5d %5d] => [%5d %5d %5d]\n",
					priv->fir.num,
					priv->fir.raw
					[priv->fir.num][BMG_AXIS_X],
					priv->fir.raw
					[priv->fir.num][BMG_AXIS_Y],
					priv->fir.raw
					[priv->fir.num][BMG_AXIS_Z],
					priv->fir.sum[BMG_AXIS_X],
					priv->fir.sum[BMG_AXIS_Y],
					priv->fir.sum[BMG_AXIS_Z]);
				}
				priv->fir.num++;
				priv->fir.idx++;
			} else {
				idx = priv->fir.idx % firlen;
				priv->fir.sum[BMG_AXIS_X] -=
					priv->fir.raw[idx][BMG_AXIS_X];
				priv->fir.sum[BMG_AXIS_Y] -=
					priv->fir.raw[idx][BMG_AXIS_Y];
				priv->fir.sum[BMG_AXIS_Z] -=
					priv->fir.raw[idx][BMG_AXIS_Z];
				priv->fir.raw[idx][BMG_AXIS_X] =
					data[BMG_AXIS_X];
				priv->fir.raw[idx][BMG_AXIS_Y] =
					data[BMG_AXIS_Y];
				priv->fir.raw[idx][BMG_AXIS_Z] =
					data[BMG_AXIS_Z];
				priv->fir.sum[BMG_AXIS_X] +=
					data[BMG_AXIS_X];
				priv->fir.sum[BMG_AXIS_Y] +=
					data[BMG_AXIS_Y];
				priv->fir.sum[BMG_AXIS_Z] +=
					data[BMG_AXIS_Z];
				priv->fir.idx++;
				data[BMG_AXIS_X] =
					priv->fir.sum[BMG_AXIS_X]/firlen;
				data[BMG_AXIS_Y] =
					priv->fir.sum[BMG_AXIS_Y]/firlen;
				data[BMG_AXIS_Z] =
					priv->fir.sum[BMG_AXIS_Z]/firlen;
				if (atomic_read(&priv->trace)&GYRO_TRC_FILTER) {
					GYRO_LOG("add [%2d]"
					"[%5d %5d %5d] =>"
					"[%5d %5d %5d] : [%5d %5d %5d]\n", idx,
					priv->fir.raw[idx][BMG_AXIS_X],
					priv->fir.raw[idx][BMG_AXIS_Y],
					priv->fir.raw[idx][BMG_AXIS_Z],
					priv->fir.sum[BMG_AXIS_X],
					priv->fir.sum[BMG_AXIS_Y],
					priv->fir.sum[BMG_AXIS_Z],
					data[BMG_AXIS_X],
					data[BMG_AXIS_Y],
					data[BMG_AXIS_Z]);
				}
			}
		}
	}
#endif
	return err;
}

#ifndef SW_CALIBRATION
/* get hardware offset value from chip register */
static int bmg_get_hw_offset(struct i2c_client *client,
		s8 offset[BMG_AXES_NUM + 1])
{
	int err = 0;
	/* HW calibration is under construction */
	GYRO_LOG("hw offset x=%x, y=%x, z=%x\n",
	offset[BMG_AXIS_X], offset[BMG_AXIS_Y], offset[BMG_AXIS_Z]);
	return err;
}
#endif

#ifndef SW_CALIBRATION
/* set hardware offset value to chip register*/
static int bmg_set_hw_offset(struct i2c_client *client,
		s8 offset[BMG_AXES_NUM + 1])
{
	int err = 0;
	/* HW calibration is under construction */
	GYRO_LOG("hw offset x=%x, y=%x, z=%x\n",
	offset[BMG_AXIS_X], offset[BMG_AXIS_Y], offset[BMG_AXIS_Z]);
	return err;
}
#endif

static int bmg_reset_calibration(struct i2c_client *client)
{
	int err = 0;
	struct bmg_i2c_data *obj = obj_i2c_data;
#ifdef SW_CALIBRATION

#else
	err = bmg_set_hw_offset(client, obj->offset);
	if (err) {
		GYRO_ERR("read hw offset failed, %d\n", err);
		return err;
	}
#endif
	memset(obj->cali_sw, 0x00, sizeof(obj->cali_sw));
	memset(obj->offset, 0x00, sizeof(obj->offset));
	return err;
}

static int bmg_read_calibration(struct i2c_client *client,
	int act[BMG_AXES_NUM], int raw[BMG_AXES_NUM])
{
	/*
	*raw: the raw calibration data, unmapped;
	*act: the actual calibration data, mapped
	*/
	int err = 0;
	int mul;
	struct bmg_i2c_data *obj = obj_i2c_data;

	#ifdef SW_CALIBRATION
	/* only sw calibration, disable hw calibration */
	mul = 0;
	#else
	err = bmg_get_hw_offset(client, obj->offset);
	if (err) {
		GYRO_ERR("read hw offset failed, %d\n", err);
		return err;
	}
	mul = 1; /* mul = sensor sensitivity / offset sensitivity */
	#endif

	raw[BMG_AXIS_X] = obj->offset[BMG_AXIS_X]*mul + obj->cali_sw[BMG_AXIS_X];
	raw[BMG_AXIS_Y] = obj->offset[BMG_AXIS_Y]*mul + obj->cali_sw[BMG_AXIS_Y];
	raw[BMG_AXIS_Z] = obj->offset[BMG_AXIS_Z]*mul + obj->cali_sw[BMG_AXIS_Z];

	act[obj->cvt.map[BMG_AXIS_X]] = obj->cvt.sign[BMG_AXIS_X]*raw[BMG_AXIS_X];
	act[obj->cvt.map[BMG_AXIS_Y]] = obj->cvt.sign[BMG_AXIS_Y]*raw[BMG_AXIS_Y];
	act[obj->cvt.map[BMG_AXIS_Z]] = obj->cvt.sign[BMG_AXIS_Z]*raw[BMG_AXIS_Z];
	return err;
}

static int bmg_write_calibration(struct i2c_client *client,
	int dat[BMG_AXES_NUM])
{
	/* dat array : Android coordinate system, mapped, unit:LSB */
	int err = 0;
	int cali[BMG_AXES_NUM] = {0};
	int raw[BMG_AXES_NUM] = {0};
	struct bmg_i2c_data *obj = obj_i2c_data;
	/*offset will be updated in obj->offset */
	err = bmg_read_calibration(client, cali, raw);
	if (err) {
		GYRO_ERR("read offset fail, %d\n", err);
		return err;
	}
	/* calculate the real offset expected by caller */
	cali[BMG_AXIS_X] += dat[BMG_AXIS_X];
	cali[BMG_AXIS_Y] += dat[BMG_AXIS_Y];
	cali[BMG_AXIS_Z] += dat[BMG_AXIS_Z];
	GYRO_LOG("UPDATE: add mapped data(%+3d %+3d %+3d)\n",
		dat[BMG_AXIS_X], dat[BMG_AXIS_Y], dat[BMG_AXIS_Z]);

#ifdef SW_CALIBRATION
	/* obj->cali_sw array : chip coordinate system, unmapped,unit:LSB */
	obj->cali_sw[BMG_AXIS_X] =
		obj->cvt.sign[BMG_AXIS_X]*(cali[obj->cvt.map[BMG_AXIS_X]]);
	obj->cali_sw[BMG_AXIS_Y] =
		obj->cvt.sign[BMG_AXIS_Y]*(cali[obj->cvt.map[BMG_AXIS_Y]]);
	obj->cali_sw[BMG_AXIS_Z] =
		obj->cvt.sign[BMG_AXIS_Z]*(cali[obj->cvt.map[BMG_AXIS_Z]]);
#else
	/* divisor = sensor sensitivity / offset sensitivity */
	int divisor = 1;
	obj->offset[BMG_AXIS_X] = (s8)(obj->cvt.sign[BMG_AXIS_X]*
		(cali[obj->cvt.map[BMG_AXIS_X]])/(divisor));
	obj->offset[BMG_AXIS_Y] = (s8)(obj->cvt.sign[BMG_AXIS_Y]*
		(cali[obj->cvt.map[BMG_AXIS_Y]])/(divisor));
	obj->offset[BMG_AXIS_Z] = (s8)(obj->cvt.sign[BMG_AXIS_Z]*
		(cali[obj->cvt.map[BMG_AXIS_Z]])/(divisor));

	/*convert software calibration using standard calibration*/
	obj->cali_sw[BMG_AXIS_X] = obj->cvt.sign[BMG_AXIS_X]*
		(cali[obj->cvt.map[BMG_AXIS_X]])%(divisor);
	obj->cali_sw[BMG_AXIS_Y] = obj->cvt.sign[BMG_AXIS_Y]*
		(cali[obj->cvt.map[BMG_AXIS_Y]])%(divisor);
	obj->cali_sw[BMG_AXIS_Z] = obj->cvt.sign[BMG_AXIS_Z]*
		(cali[obj->cvt.map[BMG_AXIS_Z]])%(divisor);
	/* HW calibration is under construction */
	err = bmg_set_hw_offset(client, obj->offset);
	if (err) {
		GYRO_ERR("read hw offset failed.\n");
		return err;
	}
#endif
	return err;
}

static int bmg_ReadCalibration_file(struct i2c_client *client)
{
	struct bmg_i2c_data *obj = obj_i2c_data;
	int fd;
	int i;
	int res;
	char temp_str[6];
	int cal_read[6];
	mm_segment_t old_fs = get_fs();

	set_fs(KERNEL_DS);

	fd = sys_open(GYRO_CALIBRATION_PATH, O_RDONLY, 0);
	if(fd < 0) {
		GYRO_ERR("File Open Error \n");
		sys_close(fd);
		return -EINVAL;
	}

	for(i = 0 ; i < 6 ; i++) {
		memset(temp_str, 0x00, sizeof(temp_str));
		res = sys_read(fd, temp_str, sizeof(temp_str));
		if(res<0){
			GYRO_ERR("Read Error \n");
			sys_close(fd);
			return -EINVAL;
		}
		sscanf(temp_str, "%d", &cal_read[i]);
	}

	sys_close(fd);
	set_fs(old_fs);
	GYRO_LOG("bmg_ReadCalibration_file Done.\n");

	for(i = 0 ; i < 6 ; i ++) {
		obj->cali_sw[i] = cal_read[i];
	}

	GYRO_LOG("data : %d %d %d / %d %d %d\n",
		obj->cali_sw[BMG_AXIS_X], obj->cali_sw[BMG_AXIS_Y], obj->cali_sw[BMG_AXIS_Z],
		obj->cali_sw[BMG_AXIS_OLD_X], obj->cali_sw[BMG_AXIS_OLD_Y], obj->cali_sw[BMG_AXIS_OLD_Z]);

	return BMI160_GYRO_SUCCESS;
}

static int bmg_WriteCalibration_file(struct i2c_client *client)
{
	struct bmg_i2c_data *obj = obj_i2c_data;
	int fd;
	int i;
	int res;
	mm_segment_t old_fs = get_fs();
	char temp_str[6];

	set_fs(KERNEL_DS);

	fd = sys_open(GYRO_CALIBRATION_PATH, O_WRONLY|O_CREAT|S_IROTH, 0666);
	if(fd < 0) {
		GYRO_ERR("File Open Error (%d)\n",fd);
		sys_close(fd);
		return -EINVAL;
	}
	for(i = 0 ; i < 6 ; i++) {
		memset(temp_str, 0x00, sizeof(temp_str));
		sprintf(temp_str, "%d", obj->cali_sw[i]);
		res = sys_write(fd,temp_str, sizeof(temp_str));

		if(res<0) {
			GYRO_ERR("Write Error \n");
			sys_close(fd);
			return -EINVAL;
		}
	}
	sys_fsync(fd);
	sys_close(fd);

	sys_chmod(GYRO_CALIBRATION_PATH, 0664);
	set_fs(old_fs);

	GYRO_LOG("bmg_WriteCalibration_file Done.\n");
	GYRO_LOG("data : %d %d %d / %d %d %d\n",
		obj->cali_sw[BMG_AXIS_X], obj->cali_sw[BMG_AXIS_Y], obj->cali_sw[BMG_AXIS_Z],
		obj->cali_sw[BMG_AXIS_OLD_X], obj->cali_sw[BMG_AXIS_OLD_Y], obj->cali_sw[BMG_AXIS_OLD_Z]);

   return BMI160_GYRO_SUCCESS;
}

static int make_cal_data_file(void)
{
	int fd;
	int i;
	int res;
	mm_segment_t old_fs = get_fs();
	char temp_str[6] ={0,};
	int cal_misc[6 ] = {0,};

	set_fs(KERNEL_DS);

	fd = sys_open(GYRO_CALIBRATION_PATH, O_RDONLY, 0); /* Cal file exist check */

	if(fd == -2)
		GYRO_LOG("Open Cal File Error. Need to make file (%d)\n",fd);

	if(fd < 0) {
		fd = sys_open(GYRO_CALIBRATION_PATH, O_WRONLY|O_CREAT|S_IROTH, 0666);
		if(fd < 0) {
			GYRO_ERR("Open or Make Cal File Error (%d)\n",fd);
			sys_close(fd);
			return -EINVAL;
		}

		for(i = 0 ; i < 6 ; i++) {
			memset(temp_str, 0x00, sizeof(temp_str));
			sprintf(temp_str, "%d", cal_misc[i]);
			res = sys_write(fd, temp_str, sizeof(temp_str));
			if(res < 0) {
				GYRO_ERR("Write Cal Error \n");
				sys_close(fd);
				return -EINVAL;
			}
		}

		GYRO_LOG("make_cal_data_file Done.\n");

	} else {
		GYRO_LOG("Sensor Cal File exist.\n");
	}

	sys_fsync(fd);
	sys_close(fd);
	sys_chmod(GYRO_CALIBRATION_PATH, 0664);
	set_fs(old_fs);

	return BMI160_GYRO_SUCCESS;
}

/* get chip type */
static int bmg_get_chip_type(struct i2c_client *client)
{
	int err = 0;
	u8 chip_id = 0;
	struct bmg_i2c_data *obj = obj_i2c_data;
	/* twice */
	err = bmg_i2c_read_block(client, BMI160_USER_CHIP_ID__REG, &chip_id, 1);
	err = bmg_i2c_read_block(client, BMI160_USER_CHIP_ID__REG, &chip_id, 1);
	if (err != 0) {
		GYRO_ERR("read chip id failed.\n");
		return err;
	}
	switch (chip_id) {
	case SENSOR_CHIP_ID_BMI:
	case SENSOR_CHIP_ID_BMI_C2:
	case SENSOR_CHIP_ID_BMI_C3:
		obj->sensor_type = BMI160_GYRO_TYPE;
		strcpy(obj->sensor_name, BMG_DEV_NAME);
		break;
	default:
		obj->sensor_type = INVALID_TYPE;
		strcpy(obj->sensor_name, UNKNOWN_DEV);
		break;
	}
	if (obj->sensor_type == INVALID_TYPE) {
		GYRO_ERR("unknown gyroscope.\n");
		return -1;
	}
	return err;
}

static int bmg_set_powermode(struct i2c_client *client,
		enum BMG_POWERMODE_ENUM power_mode)
{
	int err = 0;
	u8 data = 0;
	u8 actual_power_mode = 0;
	struct bmg_i2c_data *obj = obj_i2c_data;
	if (power_mode == obj->power_mode){
		return 0;
	}
	mutex_lock(&obj->lock);
	if (obj->sensor_type == BMI160_GYRO_TYPE) {
		if (power_mode == BMG_SUSPEND_MODE) {
			actual_power_mode = CMD_PMU_GYRO_SUSPEND;
		} else if (power_mode == BMG_NORMAL_MODE) {
			actual_power_mode = CMD_PMU_GYRO_NORMAL;
		} else {
			err = -EINVAL;
			GYRO_ERR("invalid power mode = %d\n", power_mode);
			mutex_unlock(&obj->lock);
			return err;
		}
		data = actual_power_mode;
		err += bmg_i2c_write_block(client,
			BMI160_CMD_COMMANDS__REG, &data, 1);
		mdelay(55);
	}
	if (err < 0) {
		GYRO_ERR("set power mode failed, err = %d, sensor name = %s\n",
					err, obj->sensor_name);
	}else {
		obj->power_mode = power_mode;
	}
	mutex_unlock(&obj->lock);
	GYRO_LOG("set power mode = %d ok.\n", (int)data);
	return err;
}

static int bmg_set_range(struct i2c_client *client, enum BMG_RANGE_ENUM range)
{
	u8 err = 0;
	u8 data = 0;
	u8 actual_range = 0;
	struct bmg_i2c_data *obj = obj_i2c_data;
	if (range == obj->range) {
		return 0;
	}
	mutex_lock(&obj->lock);
	if (obj->sensor_type == BMI160_GYRO_TYPE) {
		if (range == BMG_RANGE_2000)
			actual_range = BMI160_RANGE_2000;
		else if (range == BMG_RANGE_1000)
			actual_range = BMI160_RANGE_1000;
		else if (range == BMG_RANGE_500)
			actual_range = BMI160_RANGE_500;
		else if (range == BMG_RANGE_500)
			actual_range = BMI160_RANGE_250;
		else if (range == BMG_RANGE_500)
			actual_range = BMI160_RANGE_125;
		else {
			err = -EINVAL;
			GYRO_ERR("invalid range = %d\n", range);
			mutex_unlock(&obj->lock);
			return err;
		}
		err = bmg_i2c_read_block(client, BMI160_USER_GYR_RANGE__REG, &data, 1);
		data = BMG_SET_BITSLICE(data, BMI160_USER_GYR_RANGE, actual_range);
		err += bmg_i2c_write_block(client, BMI160_USER_GYR_RANGE__REG, &data, 1);
		mdelay(1);
		if (err < 0){
			GYRO_ERR("set range failed.\n");
		}else {
			obj->range = range;
			/* bitnum: 16bit */
			switch (range) {
				case BMG_RANGE_2000:
					obj->sensitivity = 16;
				break;
				case BMG_RANGE_1000:
					obj->sensitivity = 33;
				break;
				case BMG_RANGE_500:
					obj->sensitivity = 66;
				break;
				case BMG_RANGE_250:
					obj->sensitivity = 131;
				break;
				case BMG_RANGE_125:
					obj->sensitivity = 262;
				break;
				default:
					obj->sensitivity = 16;
				break;
			}
		}
	}
	mutex_unlock(&obj->lock);
	GYRO_LOG("set range ok.\n");
	return err;
}

static int bmg_set_datarate(struct i2c_client *client,
		int datarate)
{
	int err = 0;
	u8 data = 0;
	struct bmg_i2c_data *obj = obj_i2c_data;
	if (datarate == obj->datarate) {
		GYRO_LOG("set new data rate = %d, old data rate = %d\n", datarate, obj->datarate);
		return 0;
	}
	mutex_lock(&obj->lock);
	if (obj->sensor_type == BMI160_GYRO_TYPE) {
		err = bmg_i2c_read_block(client,
			BMI160_USER_GYR_CONF_ODR__REG, &data, 1);
		data = BMG_SET_BITSLICE(data,
			BMI160_USER_GYR_CONF_ODR, datarate);
		err += bmg_i2c_write_block(client,
			BMI160_USER_GYR_CONF_ODR__REG, &data, 1);
	}
	if (err < 0) {
		GYRO_ERR("set data rate failed.\n");
	}else {
		obj->datarate = datarate;
	}
	mutex_unlock(&obj->lock);
	GYRO_LOG("set data rate = %d ok.\n", datarate);
	return err;
}

static int bmg_init_client(struct i2c_client *client, int reset_cali)
{
#ifdef CONFIG_BMG_LOWPASS
	struct bmg_i2c_data *obj =
		(struct bmg_i2c_data *)obj_i2c_data;
#endif
	int err = 0;
	err = bmg_get_chip_type(client);
	if (err < 0) {
		return err;
	}
	err = bmg_set_datarate(client, BMI160_GYRO_ODR_100HZ);
	if (err < 0) {
		return err;
	}
	err = bmg_set_range(client, (enum BMG_RANGE_ENUM)BMG_RANGE_2000);
	if (err < 0) {
		return err;
	}
	err = bmg_set_powermode(client,
		(enum BMG_POWERMODE_ENUM)BMG_SUSPEND_MODE);
	if (err < 0) {
		return err;
	}
	if (0 != reset_cali) {
		/*reset calibration only in power on*/
		err = bmg_reset_calibration(client);
		if (err < 0) {
			GYRO_ERR("reset calibration failed.\n");
			return err;
		}
	}
#ifdef CONFIG_BMG_LOWPASS
	memset(&obj->fir, 0x00, sizeof(obj->fir));
#endif
	return 0;
}

/*
*Returns compensated and mapped value. unit is :degree/second
*/
static int bmg_read_sensor_data(struct i2c_client *client,
		char *buf, int bufsize)
{
	int gyro[BMG_AXES_NUM] = {0};
	int err = 0;
	struct bmg_i2c_data *obj = obj_i2c_data;
#ifdef SUPPORT_DYNAMIC_UPDATE_GYRO_BIAS
	struct dynamic_gyro_bias* d_gyro = &dynamic_gyro;
#endif
	err = bmg_read_raw_data(client, obj->data);
	if (err) {
		GYRO_ERR("bmg read raw data failed.\n");
		return err;
	} else {
#ifdef SUPPORT_DYNAMIC_UPDATE_GYRO_BIAS
		bmg_ExcuteDynamicGyroBias(d_gyro, obj->data, obj->cali_sw);
		d_gyro->is_stable = bmg_CheckStableToUpdateBias(d_gyro, obj->data, d_gyro->update_criteria_count);
#endif
		/* compensate data */
		obj->data[BMG_AXIS_X] += obj->cali_sw[BMG_AXIS_X];
		obj->data[BMG_AXIS_Y] += obj->cali_sw[BMG_AXIS_Y];
		obj->data[BMG_AXIS_Z] += obj->cali_sw[BMG_AXIS_Z];

#ifdef SUPPORT_DYNAMIC_UPDATE_GYRO_BIAS
		bmg_UpdateDynamicGyroBias(d_gyro, obj->data);
#endif

		/* remap coordinate */
		gyro[obj->cvt.map[BMG_AXIS_X]] =
			obj->cvt.sign[BMG_AXIS_X]*obj->data[BMG_AXIS_X];
		gyro[obj->cvt.map[BMG_AXIS_Y]] =
			obj->cvt.sign[BMG_AXIS_Y]*obj->data[BMG_AXIS_Y];
		gyro[obj->cvt.map[BMG_AXIS_Z]] =
			obj->cvt.sign[BMG_AXIS_Z]*obj->data[BMG_AXIS_Z];

		/* convert: LSB -> degree/second(o/s) */
		//gyro[BMG_AXIS_X] = gyro[BMG_AXIS_X]  / obj->sensitivity;
		//gyro[BMG_AXIS_Y] = gyro[BMG_AXIS_Y] / obj->sensitivity;
		//gyro[BMG_AXIS_Z] = gyro[BMG_AXIS_Z] / obj->sensitivity;

		sprintf(buf, "%x %x %x",
			gyro[BMG_AXIS_X], gyro[BMG_AXIS_Y], gyro[BMG_AXIS_Z]);
		if (atomic_read(&obj->trace) & GYRO_TRC_IOCTL)
			GYRO_LOG("gyroscope data: %s\n", buf);
	}
	return 0;
}

static int bmg_do_calibration(struct i2c_client *client)
{
	int err = 0;
	s16 data[BMG_AXES_NUM] = {0,};
	s32 sum[BMG_AXES_NUM] = {0,};
	s16 cali[6] = {0,};
	int i = 0;
	unsigned int timeout_shaking = 0;
	unsigned int shake_count = 0;
	struct bmg_i2c_data *client_data = obj_i2c_data;
	struct bmg_axis_data_t bmi160_udata_pre;
	struct bmg_axis_data_t bmi160_udata;

	GYRO_FUN();

	/*Back up previous calibration data*/
	for(i = 0 ; i < 6 ; i++){
		cali[i] = client_data->cali_sw[i];
	}

	err = bmg_reset_calibration(client);
	GYRO_LOG("Success reset current calibration data\n");
	if(err < 0) {
		GYRO_ERR("Fail reset current caibration data\n");
		return err;
	}

	err = bmg_read_raw_data(client, data);
	if (err) {
		GYRO_ERR("bmg read raw data failed.\n");
		return BMI160_GYRO_CAL_FAIL;
	}

	bmi160_udata_pre.x = data[BMG_AXIS_X];
	bmi160_udata_pre.y = data[BMG_AXIS_Y];
	bmi160_udata_pre.z = data[BMG_AXIS_Z];

	/* Defective chip */
	if ((abs(bmi160_udata_pre.x) > BMI160_DEFECT_LIMIT_GYRO)
		|| (abs(bmi160_udata_pre.y) > BMI160_DEFECT_LIMIT_GYRO)
		|| (abs(bmi160_udata_pre.z) > BMI160_DEFECT_LIMIT_GYRO)) {

		GYRO_LOG("BMI160_DEFECT_LIMIT x:%d, y:%d, z:%d\n",
		bmi160_udata_pre.x, bmi160_udata_pre.y, bmi160_udata_pre.z);

		GYRO_ERR("===============Defective chip===============\n");
		return BMI160_GYRO_CAL_FAIL;
	}

	do{
		mdelay(20);

		err = bmg_read_raw_data(client, data);
		if (err) {
			GYRO_ERR("bmg read raw data failed.\n");
			return BMI160_GYRO_CAL_FAIL;
		}

		bmi160_udata.x = data[BMG_AXIS_X];
		bmi160_udata.y = data[BMG_AXIS_Y];
		bmi160_udata.z = data[BMG_AXIS_Z];

		GYRO_LOG("===========moved x, y, z============== timeout = %d\n", timeout_shaking);
		if(shake_count > 2) {
			GYRO_ERR("==============shaking x, y, z=============\n");
			return BMI160_GYRO_CAL_FAIL;
		}

		GYRO_LOG("(%d, %d, %d) (%d, %d, %d)\n",
				bmi160_udata_pre.x,bmi160_udata_pre.y,bmi160_udata_pre.z,
				bmi160_udata.x,bmi160_udata.y, bmi160_udata.z);

		if((abs(bmi160_udata.x - bmi160_udata_pre.x) > BMI160_GYRO_SHAKING_DETECT_THRESHOLD)
				||(abs(bmi160_udata.y - bmi160_udata_pre.y) > BMI160_GYRO_SHAKING_DETECT_THRESHOLD)
				||(abs(bmi160_udata.z - bmi160_udata_pre.z) > BMI160_GYRO_SHAKING_DETECT_THRESHOLD)){
			shake_count++;
		} else {
			bmi160_udata_pre.x = bmi160_udata.x;
			bmi160_udata_pre.y = bmi160_udata.y;
			bmi160_udata_pre.z = bmi160_udata.z;

			sum[BMG_AXIS_X] += bmi160_udata.x;
			sum[BMG_AXIS_Y] += bmi160_udata.y;
			sum[BMG_AXIS_Z] += bmi160_udata.z;
		}

		timeout_shaking++;

	} while(timeout_shaking < CALIBRATION_DATA_AMOUNT);

	/* Set new calibration */
	client_data->cali_sw[BMG_AXIS_X] = -(s16)(sum[BMG_AXIS_X]/CALIBRATION_DATA_AMOUNT);
	client_data->cali_sw[BMG_AXIS_Y] = -(s16)(sum[BMG_AXIS_Y]/CALIBRATION_DATA_AMOUNT);
	client_data->cali_sw[BMG_AXIS_Z] = -(s16)(sum[BMG_AXIS_Z]/CALIBRATION_DATA_AMOUNT);

	client_data->cali_sw[BMG_AXIS_OLD_X] = cali[BMG_AXIS_X];
	client_data->cali_sw[BMG_AXIS_OLD_Y] = cali[BMG_AXIS_Y];
	client_data->cali_sw[BMG_AXIS_OLD_Z] = cali[BMG_AXIS_Z];

#ifdef SUPPORT_DYNAMIC_UPDATE_GYRO_BIAS
	client_data->cali_data[BMG_AXIS_X] = client_data->cali_sw[BMG_AXIS_X];
	client_data->cali_data[BMG_AXIS_Y] = client_data->cali_sw[BMG_AXIS_Y];
	client_data->cali_data[BMG_AXIS_Z] = client_data->cali_sw[BMG_AXIS_Z];
#endif

#ifdef CALIBRATION_TO_FILE
	err = bmg_WriteCalibration_file(client);
	if(err < 0)
			return BMI160_GYRO_CAL_FAIL;
#endif

	return BMI160_GYRO_SUCCESS;

}

#ifdef SUPPORT_DYNAMIC_UPDATE_GYRO_BIAS
static void bmg_ExcuteDynamicGyroBias(struct dynamic_gyro_bias* d_gyro,	s16* data, s16* cali)
{

	GYRO_LOG("dynamic_update_gyro_bias = %d\n", d_gyro->dynamic_update_gyro_bias);

	if(!d_gyro->dynamic_update_gyro_bias){
		s16 x_diff = (abs(data[BMG_AXIS_X]) - abs(d_gyro->gyrodata[BMG_AXIS_X]));
		s16 y_diff = (abs(data[BMG_AXIS_Y]) - abs(d_gyro->gyrodata[BMG_AXIS_Y]));
		s16 z_diff = (abs(data[BMG_AXIS_Z]) - abs(d_gyro->gyrodata[BMG_AXIS_Z]));

		//GYRO_LOG("Previous : x=%5d, y=%5d, z=%5d count=%d\n", data[BMG_AXIS_X], data[BMG_AXIS_Y], data[BMG_AXIS_Z], d_gyro->stable_check_count);
		//GYRO_LOG("Current : x=%5d, y=%5d, z=%5d count=%d\n",
		//	d_gyro->gyrodata[BMG_AXIS_X], d_gyro->gyrodata[BMG_AXIS_Y], d_gyro->gyrodata[BMG_AXIS_Z], d_gyro->stable_check_count);
		//GYRO_LOG("Diff : x=%5d, y=%5d, z=%5d count=%d\n", x_diff, y_diff, z_diff, d_gyro->stable_check_count);

		if((abs(x_diff) <= d_gyro->stable_criteria_x) &&
			(abs(y_diff) <= d_gyro->stable_criteria_y) &&
			(abs(z_diff) <= d_gyro->stable_criteria_z)){

			if(d_gyro->stable_check_count == d_gyro->stable_criteria_count){
				cali[BMG_AXIS_X] = (s16)((-1) * (d_gyro->new_bias_x / d_gyro->stable_criteria_count));
				cali[BMG_AXIS_Y] = (s16)((-1) * (d_gyro->new_bias_y / d_gyro->stable_criteria_count));
				cali[BMG_AXIS_Z] = (s16)((-1) * (d_gyro->new_bias_z / d_gyro->stable_criteria_count));

				GYRO_LOG("d_gyro Excute Gyro bias completed : gyro sum [%d, %d, %d]\n",
				d_gyro->new_bias_x, d_gyro->new_bias_y, d_gyro->new_bias_z);

				d_gyro->stable_check_count = 0;
				d_gyro->dynamic_update_gyro_bias = 1;

				GYRO_LOG("d_gyro Excute Gyro bias completed : updated bias[%d %d %d]\n",
				cali[BMG_AXIS_X],cali[BMG_AXIS_Y], cali[BMG_AXIS_Z]);

				return;
			}

			d_gyro->stable_check_count++;
			d_gyro->new_bias_x += data[BMG_AXIS_X];
			d_gyro->new_bias_y += data[BMG_AXIS_Y];
			d_gyro->new_bias_z += data[BMG_AXIS_Z];

		} else {
			// It should be checked with 50 "conitunus" data.
			d_gyro->stable_check_count = 0;
			d_gyro->new_bias_x = 0;
			d_gyro->new_bias_y = 0;
			d_gyro->new_bias_z = 0;
		}
		d_gyro->gyrodata[BMG_AXIS_X] = data[BMG_AXIS_X];
		d_gyro->gyrodata[BMG_AXIS_Y] = data[BMG_AXIS_Y];
		d_gyro->gyrodata[BMG_AXIS_Z] = data[BMG_AXIS_Z];
	}
}

static int bmg_CheckStableToUpdateBias(struct dynamic_gyro_bias* d_gyro, s16* data, int criteria)
{
	if(d_gyro->dynamic_update_gyro_bias){
		s16 x_diff = (abs(data[BMG_AXIS_X]) - abs(d_gyro->update_gyrodata[BMG_AXIS_X]));
		s16 y_diff = (abs(data[BMG_AXIS_Y]) - abs(d_gyro->update_gyrodata[BMG_AXIS_Y]));
		s16 z_diff = (abs(data[BMG_AXIS_Z]) - abs(d_gyro->update_gyrodata[BMG_AXIS_Z]));

		if((abs(x_diff) <= d_gyro->stable_criteria_x) &&
			(abs(y_diff) <= d_gyro->stable_criteria_y) &&
			(abs(z_diff) <= d_gyro->stable_criteria_z)){
			if(d_gyro->stable_check_count == criteria){
				d_gyro->stable_check_count = 0;
				return 1;
			}
			d_gyro->stable_check_count++;
		} else {
			d_gyro->stable_check_count = 0;
		}

		d_gyro->update_gyrodata[BMG_AXIS_X] = data[BMG_AXIS_X];
		d_gyro->update_gyrodata[BMG_AXIS_Y] = data[BMG_AXIS_Y];
		d_gyro->update_gyrodata[BMG_AXIS_Z] = data[BMG_AXIS_Z];

	}

	return 0;
}


static void bmg_UpdateDynamicGyroBias(struct dynamic_gyro_bias* d_gyro, int* data)
{
	if((d_gyro->is_stable) && d_gyro->dynamic_update_gyro_bias){
		if( (abs(data[BMG_AXIS_X]) > d_gyro->update_criteria_x) ||
			(abs(data[BMG_AXIS_Y]) > d_gyro->update_criteria_y) ||
			(abs(data[BMG_AXIS_Z]) > d_gyro->update_criteria_z)){
					d_gyro->dynamic_update_gyro_bias = 0;
					d_gyro->new_bias_x = 0;
					d_gyro->new_bias_y = 0;
					d_gyro->new_bias_z = 0;
					d_gyro->is_stable = 0;
					GYRO_LOG("d_gyro BMG_UpdateDynamicGyroBias() -> Excute d_gyro\n");
					return;
		}
	}
}
#endif

static ssize_t show_chipinfo_value(struct device_driver *ddri, char *buf)
{
	struct bmg_i2c_data *obj = obj_i2c_data;
	return snprintf(buf, PAGE_SIZE, "%s\n", obj->sensor_name);
}

/*
* sensor data format is hex, unit:degree/second
*/
static ssize_t show_sensordata_value(struct device_driver *ddri, char *buf)
{
	struct bmg_i2c_data *obj = obj_i2c_data;
	char strbuf[BMG_BUFSIZE] = {0};
	bmg_read_sensor_data(obj->client, strbuf, BMG_BUFSIZE);
	return snprintf(buf, PAGE_SIZE, "%s\n", strbuf);
}

/*
* raw data format is s16, unit:LSB, axis mapped
*/
static ssize_t show_rawdata_value(struct device_driver *ddri, char *buf)
{
	s16 databuf[BMG_AXES_NUM] = {0};
	s16 dataraw[BMG_AXES_NUM] = {0};
	struct bmg_i2c_data *obj = obj_i2c_data;
	bmg_read_raw_data(obj->client, dataraw);
	/*remap coordinate*/
	databuf[obj->cvt.map[BMG_AXIS_X]] =
			obj->cvt.sign[BMG_AXIS_X]*dataraw[BMG_AXIS_X];
	databuf[obj->cvt.map[BMG_AXIS_Y]] =
			obj->cvt.sign[BMG_AXIS_Y]*dataraw[BMG_AXIS_Y];
	databuf[obj->cvt.map[BMG_AXIS_Z]] =
			obj->cvt.sign[BMG_AXIS_Z]*dataraw[BMG_AXIS_Z];
	return snprintf(buf, PAGE_SIZE, "%hd %hd %hd\n",
			databuf[BMG_AXIS_X],
			databuf[BMG_AXIS_Y],
			databuf[BMG_AXIS_Z]);
}

static ssize_t show_cali_value(struct device_driver *ddri, char *buf)
{
	int err = 0;
	int len = 0;
	int mul;
	int act[BMG_AXES_NUM] = {0};
	int raw[BMG_AXES_NUM] = {0};
	struct bmg_i2c_data *obj = obj_i2c_data;
	err = bmg_read_calibration(obj->client, act, raw);
	if (err) {
		return -EINVAL;
	}
	else {
		mul = 1; /* mul = sensor sensitivity / offset sensitivity */
		len += snprintf(buf+len, PAGE_SIZE-len,
		"[HW ][%d] (%+3d, %+3d, %+3d) : (0x%02X, 0x%02X, 0x%02X)\n",
		mul,
		obj->offset[BMG_AXIS_X],
		obj->offset[BMG_AXIS_Y],
		obj->offset[BMG_AXIS_Z],
		obj->offset[BMG_AXIS_X],
		obj->offset[BMG_AXIS_Y],
		obj->offset[BMG_AXIS_Z]);
		len += snprintf(buf+len, PAGE_SIZE-len,
		"[SW ][%d] (%+3d, %+3d, %+3d)\n", 1,
		obj->cali_sw[BMG_AXIS_X],
		obj->cali_sw[BMG_AXIS_Y],
		obj->cali_sw[BMG_AXIS_Z]);
		len += snprintf(buf+len, PAGE_SIZE-len,
		"[ALL]unmapped(%+3d, %+3d, %+3d), mapped(%+3d, %+3d, %+3d)\n",
		raw[BMG_AXIS_X], raw[BMG_AXIS_Y], raw[BMG_AXIS_Z],
		act[BMG_AXIS_X], act[BMG_AXIS_Y], act[BMG_AXIS_Z]);
		return len;
	}
}

static ssize_t store_cali_value(struct device_driver *ddri,
		const char *buf, size_t count)
{
	int err = 0;
	int dat[BMG_AXES_NUM] = {0};
	struct bmg_i2c_data *obj = obj_i2c_data;
	if (!strncmp(buf, "rst", 3)) {
		err = bmg_reset_calibration(obj->client);
		if (err)
			GYRO_ERR("reset offset err = %d\n", err);
	} else if (BMG_AXES_NUM == sscanf(buf, "0x%02X 0x%02X 0x%02X",
		&dat[BMG_AXIS_X], &dat[BMG_AXIS_Y], &dat[BMG_AXIS_Z])) {
		err = bmg_write_calibration(obj->client, dat);
		if (err) {
			GYRO_ERR("bmg write calibration failed, err = %d\n",
				err);
		}
	} else {
		GYRO_ERR("invalid format\n");
	}
	return count;
}

static ssize_t show_firlen_value(struct device_driver *ddri, char *buf)
{
#ifdef CONFIG_BMG_LOWPASS
	struct i2c_client *client = bmg222_i2c_client;
	struct bmg_i2c_data *obj = obj_i2c_data;
	if (atomic_read(&obj->firlen)) {
		int idx, len = atomic_read(&obj->firlen);
		GYRO_LOG("len = %2d, idx = %2d\n", obj->fir.num, obj->fir.idx);

		for (idx = 0; idx < len; idx++) {
			GYRO_LOG("[%5d %5d %5d]\n",
			obj->fir.raw[idx][BMG_AXIS_X],
			obj->fir.raw[idx][BMG_AXIS_Y],
			obj->fir.raw[idx][BMG_AXIS_Z]);
		}

		GYRO_LOG("sum = [%5d %5d %5d]\n",
			obj->fir.sum[BMG_AXIS_X],
			obj->fir.sum[BMG_AXIS_Y],
			obj->fir.sum[BMG_AXIS_Z]);
		GYRO_LOG("avg = [%5d %5d %5d]\n",
			obj->fir.sum[BMG_AXIS_X]/len,
			obj->fir.sum[BMG_AXIS_Y]/len,
			obj->fir.sum[BMG_AXIS_Z]/len);
	}
	return snprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&obj->firlen));
#else
	return snprintf(buf, PAGE_SIZE, "not support\n");
#endif
}

static ssize_t store_firlen_value(struct device_driver *ddri,
		const char *buf, size_t count)
{
#ifdef CONFIG_BMG_LOWPASS
	struct i2c_client *client = bmg222_i2c_client;
	struct bmg_i2c_data *obj = obj_i2c_data;
	int firlen;

	if (1 != sscanf(buf, "%d", &firlen)) {
		GYRO_ERR("invallid format\n");
	} else if (firlen > C_MAX_FIR_LENGTH) {
		GYRO_ERR("exceeds maximum filter length\n");
	} else {
		atomic_set(&obj->firlen, firlen);
		if (NULL == firlen) {
			atomic_set(&obj->fir_en, 0);
		} else {
			memset(&obj->fir, 0x00, sizeof(obj->fir));
			atomic_set(&obj->fir_en, 1);
		}
	}
#endif

	return count;
}

static ssize_t show_trace_value(struct device_driver *ddri, char *buf)
{
	ssize_t res;
	struct bmg_i2c_data *obj = obj_i2c_data;
	res = snprintf(buf, PAGE_SIZE, "0x%04X\n", atomic_read(&obj->trace));
	return res;
}

static ssize_t store_trace_value(struct device_driver *ddri,
		const char *buf, size_t count)
{
	int trace;
	struct bmg_i2c_data *obj = obj_i2c_data;
	if (1 == sscanf(buf, "0x%x", &trace))
		atomic_set(&obj->trace, trace);
	else
		GYRO_ERR("invalid content: '%s'\n", buf);
	return count;
}

static ssize_t show_status_value(struct device_driver *ddri, char *buf)
{
	ssize_t len = 0;
	struct bmg_i2c_data *obj = obj_i2c_data;
	if (obj->hw)
		len += snprintf(buf+len, PAGE_SIZE-len,
		"CUST: %d %d (%d %d)\n",
		obj->hw->i2c_num, obj->hw->direction,
		obj->hw->power_id, obj->hw->power_vol);
	else
		len += snprintf(buf+len, PAGE_SIZE-len, "CUST: NULL\n");

	len += snprintf(buf+len, PAGE_SIZE-len, "i2c addr:%#x,ver:%s\n",
		obj->client->addr, BMG_DRIVER_VERSION);
	return len;
}

static ssize_t show_power_mode_value(struct device_driver *ddri, char *buf)
{
	ssize_t len = 0;
	struct bmg_i2c_data *obj = obj_i2c_data;
	len += snprintf(buf+len, PAGE_SIZE-len, "%s mode\n",
		obj->power_mode == BMG_NORMAL_MODE ? "normal" : "suspend");
	return len;
}

static ssize_t store_power_mode_value(struct device_driver *ddri,
		const char *buf, size_t count)
{
	int err;
	unsigned long power_mode;
	struct bmg_i2c_data *obj = obj_i2c_data;
	err = kstrtoul(buf, 10, &power_mode);
	if(err < 0) {
		return err;
	}
	if(BMI_GYRO_PM_NORMAL == power_mode){
		err = bmg_set_powermode(obj->client, BMG_NORMAL_MODE);
	}else {
		err = bmg_set_powermode(obj->client, BMG_SUSPEND_MODE);
	}
	if(err < 0) {
		GYRO_ERR("set power mode failed.\n");
	}
	return err;
}

static ssize_t show_range_value(struct device_driver *ddri, char *buf)
{
	ssize_t len = 0;
	struct bmg_i2c_data *obj = obj_i2c_data;
	len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", obj->range);
	return len;
}

static ssize_t store_range_value(struct device_driver *ddri,
		const char *buf, size_t count)
{
	struct bmg_i2c_data *obj = obj_i2c_data;
	unsigned long range;
	int err;
	err = kstrtoul(buf, 10, &range);
	if (err == 0) {
		if ((range == BMG_RANGE_2000)
		|| (range == BMG_RANGE_1000)
		|| (range == BMG_RANGE_500)
		|| (range == BMG_RANGE_250)
		|| (range == BMG_RANGE_125)) {
			err = bmg_set_range(obj->client, range);
			if (err) {
				GYRO_ERR("set range value failed.\n");
				return err;
			}
		}
	}
	return count;
}

static ssize_t show_datarate_value(struct device_driver *ddri, char *buf)
{
	ssize_t len = 0;
	struct bmg_i2c_data *obj = obj_i2c_data;
	len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", obj->datarate);
	return len;
}

static ssize_t store_datarate_value(struct device_driver *ddri,
		const char *buf, size_t count)
{
	int err;
	struct bmg_i2c_data *obj = obj_i2c_data;

	unsigned long datarate;

	err = kstrtoul(buf, 10, &datarate);
	if(err < 0) {
		return err;
	}
	err = bmg_set_datarate(obj->client, datarate);
	if(err < 0) {
		GYRO_ERR("set data rate failed.\n");
		return err;
	}
	return count;

}

static ssize_t show_run_fast_calibration(struct device_driver *ddri, char *buf)
{
	ssize_t len = 0;
	struct bmg_i2c_data *obj = obj_i2c_data;
	len += snprintf(buf+len, PAGE_SIZE-len, "%d\n", obj->fast_calib_rslt);
	return len;
}

static ssize_t store_run_fast_calibration(struct device_driver *ddri,
		const char *buf, size_t count)
{
	int res = 0;
	struct bmg_i2c_data *obj = obj_i2c_data;
	struct i2c_client *client = obj->client;

	atomic_set(&obj->fast_calib_rslt, BMI160_GYRO_SUCCESS);

	res = bmg_do_calibration(client);
	if(res != BMI160_GYRO_SUCCESS) {
		GYRO_ERR("Calibration Fail err=%d\n", res);
		atomic_set(&obj->fast_calib_rslt, BMI160_GYRO_CAL_FAIL);
	} else {
		GYRO_LOG("Calibraiton Success\n");
	}

	return count;
}


static int bmi160_set_gyro_self_test_start(u8 gyro_self_test_start)
{
	int comres = 0;
	u8 v_data_u8r = 0;
	struct bmg_i2c_data *obj = obj_i2c_data;

	GYRO_FUN();
/*
	v_data_u8r = CMD_RESET_USER_REG;
	comres = bmg_i2c_write_block(obj->client, BMI160_CMD_COMMANDS__REG, &v_data_u8r, 1);

	GYRO_LOG("Soft Reset");
	mdelay(55);
	if (comres < 0) {
		GYRO_ERR("Soft reset error, err = %d, sensor name = %s\n",
					comres, obj->sensor_name);
	}
*/
	comres = bmg_set_powermode(obj->client, BMG_NORMAL_MODE);
	if(comres){
		GYRO_ERR("set power mode failed, err = %d\n", comres);
	}

	if(gyro_self_test_start < 2){
		comres += bmg_i2c_read_block(obj->client, BMI160_USER_GYRO_SELF_TEST_START__REG, &v_data_u8r, 1);
		if(comres == 0){
			v_data_u8r = BMG_SET_BITSLICE(v_data_u8r,BMI160_USER_GYRO_SELF_TEST_START,gyro_self_test_start);
			GYRO_ERR("bmi160 start setting %x \n",v_data_u8r);
			comres += bmg_i2c_write_block(obj->client, BMI160_USER_GYRO_SELF_TEST_START__REG, &v_data_u8r, 1);
		}
	}else{
		comres = EBMI160_OUT_OF_RANGE;
	}
	return comres;
}

static int bmi160_get_gyro_self_test(u8 *gyro_self_test_ok)
{
	int comres = 0;
	u8 v_data_u8r = 0;
	struct bmg_i2c_data *obj = obj_i2c_data;

	comres += bmg_i2c_read_block(obj->client, BMI160_USER_STATUS_GYRO_SELFTEST_OK__REG, &v_data_u8r, 1);
	GYRO_ERR("bmi160 get status %x\n",v_data_u8r);
	*gyro_self_test_ok = BMG_GET_BITSLICE(v_data_u8r, BMI160_USER_STATUS_GYRO_SELFTEST_OK);
	
	GYRO_ERR("bmi160 get selftest status %d\n",comres);

	return comres;
}

static ssize_t bmi160_selftest_show(struct device_driver *ddri, char *buf)
{
	ssize_t len = 0;
	struct bmg_i2c_data *obj = obj_i2c_data;
	return sprintf(buf, "0x%x\n", atomic_read(&obj->selftest_result));
}

static ssize_t bmi160_selftest_store(struct device_driver *ddri,
		const char *buf, size_t count)
{
	u8 gyro_selftest = 0;
	struct bmg_i2c_data *obj = obj_i2c_data;
	int err = 0;
	u8 data;

	GYRO_ERR("GYRO_SELFTEST START\n");
	err += bmi160_set_gyro_self_test_start(1);
	
	msleep(60);

	err += bmi160_get_gyro_self_test(&gyro_selftest);
	atomic_set(&obj->selftest_result, gyro_selftest);
	GYRO_ERR("GYRO_SELFTEST FINISHED : %d\n", gyro_selftest);
	
	return count;
}

#ifdef SUPPORT_DYNAMIC_UPDATE_GYRO_BIAS
static ssize_t bmi160_dynamic_gyro_bias_show(struct device_driver *ddri, char *buf)
{
	ssize_t len = 0;
	struct dynamic_gyro_bias* d_gyro = &dynamic_gyro;

	len += snprintf(buf+len, PAGE_SIZE-len, "stable_criteria_x = %d\n", d_gyro->stable_criteria_x);
	len += snprintf(buf+len, PAGE_SIZE-len, "stable_criteria_y = %d\n", d_gyro->stable_criteria_y);
	len += snprintf(buf+len, PAGE_SIZE-len, "stable_criteria_z = %d\n", d_gyro->stable_criteria_z);

	return len;
}

static ssize_t bmi160_dynamic_gyro_bias_store(struct device_driver *ddri,
		const char *buf, size_t count)
{
	int err;
	struct dynamic_gyro_bias* d_gyro = &dynamic_gyro;

	unsigned long criteria;

	err = kstrtoul(buf, 10, &criteria);
	if(err < 0) {
		return err;
	}

	d_gyro->stable_criteria_x = criteria;
	d_gyro->stable_criteria_y = criteria;
	d_gyro->stable_criteria_z = criteria;

	GYRO_ERR("Dynamic Gyro Bias : %d %d %d\n",
		d_gyro->stable_criteria_x, d_gyro->stable_criteria_y, d_gyro->stable_criteria_z);

	return count;
}
#endif

static DRIVER_ATTR(chipinfo, S_IWUSR | S_IRUGO, show_chipinfo_value, NULL);
static DRIVER_ATTR(sensordata, S_IWUSR | S_IRUGO, show_sensordata_value, NULL);
static DRIVER_ATTR(rawdata, S_IWUSR | S_IRUGO, show_rawdata_value, NULL);
static DRIVER_ATTR(cali, S_IWUSR | S_IRUGO, show_cali_value, store_cali_value);
static DRIVER_ATTR(firlen, S_IWUSR | S_IRUGO,
		show_firlen_value, store_firlen_value);
static DRIVER_ATTR(trace, S_IWUSR | S_IRUGO,
		show_trace_value, store_trace_value);
static DRIVER_ATTR(status, S_IRUGO, show_status_value, NULL);
static DRIVER_ATTR(gyro_op_mode, S_IWUSR | S_IRUGO,
		show_power_mode_value, store_power_mode_value);
static DRIVER_ATTR(gyro_range, S_IWUSR | S_IRUGO,
		show_range_value, store_range_value);
static DRIVER_ATTR(gyro_odr, S_IWUSR | S_IRUGO,
		show_datarate_value, store_datarate_value);
static DRIVER_ATTR(run_fast_calibration, S_IRUGO|S_IWUSR|S_IWGRP,
		show_run_fast_calibration, store_run_fast_calibration);
static DRIVER_ATTR(selftest, S_IRUGO|S_IWUSR|S_IWGRP,
		bmi160_selftest_show, bmi160_selftest_store);
#ifdef SUPPORT_DYNAMIC_UPDATE_GYRO_BIAS
static DRIVER_ATTR(dynamic_bias, S_IRUGO|S_IWUSR|S_IWGRP,
		bmi160_dynamic_gyro_bias_show, bmi160_dynamic_gyro_bias_store);
#endif

static struct driver_attribute *bmg_attr_list[] = {
	/* chip information */
	&driver_attr_chipinfo,
	/* dump sensor data */
	&driver_attr_sensordata,
	/* dump raw data */
	&driver_attr_rawdata,
	/* show calibration data */
	&driver_attr_cali,
	/* filter length: 0: disable, others: enable */
	&driver_attr_firlen,
	/* trace flag */
	&driver_attr_trace,
	/* get hw configuration */
	&driver_attr_status,
	/* get power mode */
	&driver_attr_gyro_op_mode,
	/* get range */
	&driver_attr_gyro_range,
	/* get data rate */
	&driver_attr_gyro_odr,
	/* calibration */
	&driver_attr_run_fast_calibration,
	/* selftest */
	&driver_attr_selftest,
#ifdef SUPPORT_DYNAMIC_UPDATE_GYRO_BIAS
	&driver_attr_dynamic_bias,
#endif
};

static int bmg_create_attr(struct device_driver *driver)
{
	int idx = 0;
	int err = 0;
	int num = (int)(sizeof(bmg_attr_list)/sizeof(bmg_attr_list[0]));
	if (driver == NULL) {
		return -EINVAL;
	}
	for (idx = 0; idx < num; idx++) {
		err = driver_create_file(driver, bmg_attr_list[idx]);
		if (err) {
			GYRO_ERR("driver_create_file (%s) = %d\n",
				bmg_attr_list[idx]->attr.name, err);
			break;
		}
	}
	return err;
}

static int bmg_delete_attr(struct device_driver *driver)
{
	int idx, err = 0;
	int num = (int)(sizeof(bmg_attr_list)/sizeof(bmg_attr_list[0]));
	for (idx = 0; idx < num; idx++) {
		driver_remove_file(driver, bmg_attr_list[idx]);
	}
	return err;
}

int gyroscope_operate(void *self, uint32_t command, void *buff_in, int size_in,
		void *buff_out, int size_out, int *actualout)
{
	int err = 0;
	int value, sample_delay;
	char buff[BMG_BUFSIZE] = {0};
	struct bmg_i2c_data *priv = (struct bmg_i2c_data *)self;
	struct hwm_sensor_data *gyroscope_data;
	switch (command) {
	case SENSOR_DELAY:
	if ((buff_in == NULL) || (size_in < sizeof(int))) {
		GYRO_ERR("set delay parameter error\n");
		err = -EINVAL;
	} else {
		value = *(int *)buff_in;

		/*
		*Currently, fix data rate to 100Hz.
		*/
		sample_delay = BMI160_GYRO_ODR_100HZ;

		GYRO_LOG("sensor delay command: %d, sample_delay = %d\n",
			value, sample_delay);

		err = bmg_set_datarate(priv->client, sample_delay);
		if (err < 0)
			GYRO_ERR("set delay parameter error\n");

		if (value >= 40)
			atomic_set(&priv->filter, 0);
		else {
		#if defined(CONFIG_BMG_LOWPASS)
			priv->fir.num = 0;
			priv->fir.idx = 0;
			priv->fir.sum[BMG_AXIS_X] = 0;
			priv->fir.sum[BMG_AXIS_Y] = 0;
			priv->fir.sum[BMG_AXIS_Z] = 0;
			atomic_set(&priv->filter, 1);
		#endif
		}
	}
	break;
	case SENSOR_ENABLE:
	if ((buff_in == NULL) || (size_in < sizeof(int))) {
		GYRO_ERR("enable sensor parameter error\n");
		err = -EINVAL;
	} else {
		/* value:[0--->suspend, 1--->normal] */
		value = *(int *)buff_in;
		GYRO_LOG("sensor enable/disable command: %s\n",
			value ? "enable" : "disable");

		err = bmg_set_powermode(priv->client,
			(enum BMG_POWERMODE_ENUM)(!!value));
		if (err)
			GYRO_ERR("set power mode failed, err = %d\n", err);
	}
	break;
	case SENSOR_GET_DATA:
	if ((buff_out == NULL) || (size_out < sizeof(struct hwm_sensor_data))) {
		GYRO_ERR("get sensor data parameter error\n");
		err = -EINVAL;
	} else {
		gyroscope_data = (struct hwm_sensor_data *)buff_out;
		bmg_read_sensor_data(priv->client, buff, BMG_BUFSIZE);
		sscanf(buff, "%x %x %x", &gyroscope_data->values[0],
			&gyroscope_data->values[1], &gyroscope_data->values[2]);
		gyroscope_data->status = SENSOR_STATUS_ACCURACY_MEDIUM;
		gyroscope_data->value_divide = DEGREE_TO_RAD;
	}
	break;
	default:
	GYRO_ERR("gyroscope operate function no this parameter %d\n", command);
	err = -1;
	break;
	}

	return err;
}

static int bmg_open(struct inode *inode, struct file *file)
{
	file->private_data = obj_i2c_data;
	if (file->private_data == NULL) {
		GYRO_ERR("file->private_data is null pointer.\n");
		return -EINVAL;
	}
	return nonseekable_open(inode, file);
}

static int bmg_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

static long bmg_unlocked_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	int err = 0;
	char strbuf[BMG_BUFSIZE] = {0};
	int raw_offset[BMG_BUFSIZE] = {0};
	void __user *data;
	struct SENSOR_DATA sensor_data;
	int cali[BMG_AXES_NUM] = {0};
	struct bmg_i2c_data *obj = (struct bmg_i2c_data *)file->private_data;
	struct i2c_client *client = obj->client;
	if (obj == NULL)
		return -EFAULT;
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE,
			(void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok(VERIFY_READ,
			(void __user *)arg, _IOC_SIZE(cmd));
	if (err) {
		GYRO_ERR("access error: %08x, (%2d, %2d)\n",
			cmd, _IOC_DIR(cmd), _IOC_SIZE(cmd));
		return -EFAULT;
	}
	switch (cmd) {
		case GYROSCOPE_IOCTL_INIT:
			bmg_init_client(client, 0);
			err = bmg_set_powermode(client, BMG_NORMAL_MODE);
			if (err) {
				err = -EFAULT;
				break;
			}
			break;
		case GYROSCOPE_IOCTL_READ_SENSORDATA:
			data = (void __user *) arg;
			if (data == NULL) {
				err = -EINVAL;
				break;
			}

			bmg_read_sensor_data(client, strbuf, BMG_BUFSIZE);
			if (copy_to_user(data, strbuf, strlen(strbuf) + 1)) {
				err = -EFAULT;
				break;
			}
			break;
		case GYROSCOPE_IOCTL_SET_CALI:
			/* data unit is degree/second */
			data = (void __user *)arg;
			if (data == NULL) {
				err = -EINVAL;
				break;
			}
			if (copy_from_user(&sensor_data, data, sizeof(sensor_data))) {
				err = -EFAULT;
				break;
			}
#ifdef CALIBRATION_TO_FILE
			if(make_cal_data_file() == BMI160_GYRO_SUCCESS) {
				err = bmg_ReadCalibration_file(client);
				if(err != BMI160_GYRO_SUCCESS) {
					GYRO_ERR("Read Cal Fail from file !!!\n");
					break;
				} else {
					sensor_data.x = obj->cali_sw[BMG_AXIS_X];
					sensor_data.y = obj->cali_sw[BMG_AXIS_Y];
					sensor_data.z = obj->cali_sw[BMG_AXIS_Z];
				}
			}
#endif
			if (atomic_read(&obj->suspend)) {
				GYRO_ERR("perform calibration in suspend mode\n");
				err = -EINVAL;
			} else {
#ifndef CALIBRATION_TO_FILE
				/* convert: degree/second -> LSB */
				cali[BMG_AXIS_X] = sensor_data.x * obj->sensitivity;
				cali[BMG_AXIS_Y] = sensor_data.y * obj->sensitivity;
				cali[BMG_AXIS_Z] = sensor_data.z * obj->sensitivity;
				err = bmg_write_calibration(client, cali);
#endif
			}
			break;
		case GYROSCOPE_IOCTL_CLR_CALI:
			err = bmg_reset_calibration(client);
			break;
		case GYROSCOPE_IOCTL_GET_CALI:
			data = (void __user *)arg;
			if (data == NULL) {
				err = -EINVAL;
				break;
			}
			err = bmg_read_calibration(client, cali, raw_offset);
			if (err)
				break;

			sensor_data.x = cali[BMG_AXIS_X];
			sensor_data.y = cali[BMG_AXIS_Y];
			sensor_data.z = cali[BMG_AXIS_Z];
			if (copy_to_user(data, &sensor_data, sizeof(sensor_data))) {
				err = -EFAULT;
				break;
			}
			break;
		default:
			GYRO_ERR("unknown IOCTL: 0x%08x\n", cmd);
			err = -ENOIOCTLCMD;
			break;
		}
	return err;
}

static const struct file_operations bmg_fops = {
	.owner = THIS_MODULE,
	.open = bmg_open,
	.release = bmg_release,
	.unlocked_ioctl = bmg_unlocked_ioctl,
};

static struct miscdevice bmg_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "gyroscope",
	.fops = &bmg_fops,
};

static int bmg_suspend(struct i2c_client *client, pm_message_t msg)
{
	struct bmg_i2c_data *obj = obj_i2c_data;
	int err = 0;
	GYRO_FUN();
	if (msg.event == PM_EVENT_SUSPEND) {
		if (obj == NULL) {
			GYRO_ERR("null pointer\n");
			return -EINVAL;
		}
		atomic_set(&obj->suspend, 1);
		err = bmg_set_powermode(obj->client, BMG_SUSPEND_MODE);
		if (err) {
			GYRO_ERR("bmg set suspend mode failed.\n");
		}
	}
	return err;
}

static int bmg_resume(struct i2c_client *client)
{
	int err;
	struct bmg_i2c_data *obj = obj_i2c_data;
	GYRO_FUN();
	err = bmg_init_client(obj->client, 0);
	if (err) {
		GYRO_ERR("initialize client failed, err = %d\n", err);
		return err;
	}
#ifdef SUPPORT_DYNAMIC_UPDATE_GYRO_BIAS
	dynamic_gyro.dynamic_update_gyro_bias = 0;
	dynamic_gyro.stable_check_count = 0;
	dynamic_gyro.new_bias_x = 0;
	dynamic_gyro.new_bias_y = 0;
	dynamic_gyro.new_bias_z = 0;
	//GYRO_ERR("d_gyro Gyro resume, init bias %d %d %d\n", obj->cali_sw[0], obj->cali_sw[1], obj->cali_sw[2]);
#endif

	atomic_set(&obj->suspend, 0);
	return 0;
}

static int bmg_i2c_detect(struct i2c_client *client,
		struct i2c_board_info *info)
{
	strcpy(info->type, BMG_DEV_NAME);
	return 0;
}

static int bmi160_gyro_open_report_data(int open)
{
	//should queuq work to report event if  is_report_input_direct=true
	return 0;
}

static int bmi160_gyro_enable_nodata(int en)
{
	int err =0;
	int retry = 0;
	bool power = false;
	if(1 == en) {
		power = true;
	}else {
		power = false;
	}
	for(retry = 0; retry < 3; retry++){
		err = bmg_set_powermode(obj_i2c_data->client, power);
		if(err == 0) {
			GYRO_LOG("bmi160_gyro_SetPowerMode ok.\n");
			break;
		}
	}
	if(err < 0) {
		GYRO_LOG("bmi160_gyro_SetPowerMode fail!\n");
	}
	return err;
}

static int bmi160_gyro_set_delay(u64 ns)
{
	int err;
	int value = (int)ns/1000/1000 ;
	/* Currently, fix data rate to 100Hz. */
	int sample_delay = BMI160_GYRO_ODR_100HZ;
	struct bmg_i2c_data *priv = obj_i2c_data;
	err = bmg_set_datarate(priv->client, sample_delay);
	if (err < 0)
		GYRO_ERR("set data rate failed.\n");
	if (value >= 40)
		atomic_set(&priv->filter, 0);
	else {
#if defined(CONFIG_BMG_LOWPASS)
		priv->fir.num = 0;
		priv->fir.idx = 0;
		priv->fir.sum[BMG_AXIS_X] = 0;
		priv->fir.sum[BMG_AXIS_Y] = 0;
		priv->fir.sum[BMG_AXIS_Z] = 0;
		atomic_set(&priv->filter, 1);
#endif
	}
	GYRO_LOG("set gyro delay = %d\n", sample_delay);
	return 0;
}

static int bmi160_gyro_get_data(int* x ,int* y,int* z, int* status)
{
	char buff[BMG_BUFSIZE] = {0};
	bmg_read_sensor_data(obj_i2c_data->client, buff, BMG_BUFSIZE);
	sscanf(buff, "%x %x %x", x, y, z);		
	*status = SENSOR_STATUS_ACCURACY_MEDIUM;
	return 0;
}

static int bmg_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int err = 0;
	struct bmg_i2c_data *obj;
	struct gyro_control_path ctl = {0};
	struct gyro_data_path data = {0};
	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj) {
		err = -ENOMEM;
		goto exit;
	}
	obj->hw = hw;
	err = hwmsen_get_convert(obj->hw->direction, &obj->cvt);
	if (err) {
		GYRO_ERR("invalid direction: %d\n", obj->hw->direction);
		goto exit_hwmsen_get_convert_failed;
	}
	obj_i2c_data = obj;
	obj->client = bmi160_acc_i2c_client;
	i2c_set_clientdata(obj->client, obj);
	atomic_set(&obj->trace, 0);
	atomic_set(&obj->suspend, 0);
	obj->power_mode = BMG_UNDEFINED_POWERMODE;
	obj->range = BMG_UNDEFINED_RANGE;
	obj->datarate = BMI160_GYRO_ODR_RESERVED;
	mutex_init(&obj->lock);
#ifdef CONFIG_BMG_LOWPASS
	if (obj->hw->firlen > C_MAX_FIR_LENGTH)
		atomic_set(&obj->firlen, C_MAX_FIR_LENGTH);
	else
		atomic_set(&obj->firlen, obj->hw->firlen);

	if (atomic_read(&obj->firlen) > 0)
		atomic_set(&obj->fir_en, 1);
#endif
	err = bmg_init_client(obj->client, 1);
	if (err)
		goto exit_init_client_failed;

	err = misc_register(&bmg_device);
	if (err) {
		GYRO_ERR("misc device register failed, err = %d\n", err);
		goto exit_misc_device_register_failed;
	}
	err = bmg_create_attr(&bmi160_gyro_init_info.platform_diver_addr->driver);
	if (err) {
		GYRO_ERR("create attribute failed, err = %d\n", err);
		goto exit_create_attr_failed;
	}
	ctl.open_report_data= bmi160_gyro_open_report_data;
	ctl.enable_nodata = bmi160_gyro_enable_nodata;
	ctl.set_delay  = bmi160_gyro_set_delay;
	ctl.is_report_input_direct = false;
	ctl.is_support_batch = obj->hw->is_batch_supported;
	err = gyro_register_control_path(&ctl);
	if(err) {
		GYRO_ERR("register gyro control path err\n");
		goto exit_create_attr_failed;
	}

	data.get_data = bmi160_gyro_get_data;
	data.vender_div = DEGREE_TO_RAD;
	err = gyro_register_data_path(&data);
	if(err) {
		GYRO_ERR("gyro_register_data_path fail = %d\n", err);
		goto exit_create_attr_failed;
	}
#ifdef SUPPORT_DYNAMIC_UPDATE_GYRO_BIAS
	dynamic_gyro.stable_criteria_x = STABLE_DEFAULT_CRITERIA_X;
	dynamic_gyro.stable_criteria_y = STABLE_DEFAULT_CRITERIA_Y;
	dynamic_gyro.stable_criteria_z = STABLE_DEFAULT_CRITERIA_Z;
	dynamic_gyro.stable_criteria_count = STABLE_DEFAULT_CRITERIA_COUNT;
	GYRO_LOG("d_gyro applied each axis criteria (%d, %d, %d) , criteria count (%d)\n",
		dynamic_gyro.stable_criteria_x, dynamic_gyro.stable_criteria_y, dynamic_gyro.stable_criteria_z,
		dynamic_gyro.stable_criteria_count);

	dynamic_gyro.update_criteria_x = UPDATE_DEFAULT_CRITERIA_X;
	dynamic_gyro.update_criteria_y = UPDATE_DEFAULT_CRITERIA_Y;
	dynamic_gyro.update_criteria_z = UPDATE_DEFAULT_CRITERIA_Z;
	dynamic_gyro.update_criteria_count = UPDATE_DEFAULT_CRITERIA_COUNT;
	GYRO_LOG("d_gyro applied each axis criteria (%d, %d, %d) , criteria count (%d)\n",
		dynamic_gyro.update_criteria_x, dynamic_gyro.update_criteria_y, dynamic_gyro.update_criteria_z,
		dynamic_gyro.update_criteria_count);

		dynamic_gyro.dynamic_update_gyro_bias = 0;
		dynamic_gyro.stable_check_count = 0;
		dynamic_gyro.new_bias_x = 0;
		dynamic_gyro.new_bias_y = 0;
		dynamic_gyro.new_bias_z = 0;

		obj->cali_sw[0] = obj->cali_data[0];
		obj->cali_sw[1] = obj->cali_data[1];
		obj->cali_sw[2] = obj->cali_data[2];
		GYRO_ERR("d_gyro Gyro sensor enabled, init dynamic_update_gyro_bias to 0\n");
#endif

	bmi160_gyro_init_flag =0;
	GYRO_LOG("%s: OK\n", __func__);
	return 0;

exit_create_attr_failed:
	misc_deregister(&bmg_device);
exit_misc_device_register_failed:
exit_init_client_failed:
exit_hwmsen_get_convert_failed:
	kfree(obj);
exit:
	bmi160_gyro_init_flag =-1;
	GYRO_ERR("err = %d\n", err);
	return err;
}

static int bmg_i2c_remove(struct i2c_client *client)
{
	int err = 0;

	err = bmg_delete_attr(&bmi160_gyro_init_info.platform_diver_addr->driver);
	if (err)
		GYRO_ERR("bmg_delete_attr failed, err = %d\n", err);

	err = misc_deregister(&bmg_device);
	if (err)
		GYRO_ERR("misc_deregister failed, err = %d\n", err);

	obj_i2c_data = NULL;
	i2c_unregister_device(client);
	kfree(i2c_get_clientdata(client));

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id gyro_of_match[] = {
	{.compatible = "mediatek,gyro"},
	{},
};
#endif

static struct i2c_driver bmg_i2c_driver = {
	.driver = {
		.name = BMG_DEV_NAME,
#ifdef CONFIG_OF
		.of_match_table = gyro_of_match,
#endif
	},
	.probe = bmg_i2c_probe,
	.remove	= bmg_i2c_remove,
	.detect	= bmg_i2c_detect,
	.suspend = bmg_suspend,
	.resume = bmg_resume,
	.id_table = bmg_i2c_id,
};

static int bmi160_gyro_remove(void)
{
    i2c_del_driver(&bmg_i2c_driver);
    return 0;
}

static int bmi160_gyro_local_init(struct platform_device *pdev)
{
	/* compulsory registration for testing
	struct i2c_adapter *adapter;
	struct i2c_client *client;
	struct i2c_board_info info;
	adapter = i2c_get_adapter(3);
	if(adapter == NULL){
		printk(KERN_ERR"gsensor_local_init error");
	}
	memset(&info,0,sizeof(struct i2c_board_info));
	info.addr = 0x61;
	//info.type = "mediatek,gsensor";
	strlcpy(info.type,"bmi160_gyro",I2C_NAME_SIZE);
	client = i2c_new_device(adapter,&info);
	strlcpy(client->name,"bmi160_gyro",I2C_NAME_SIZE);
	*/
	if(i2c_add_driver(&bmg_i2c_driver)) {
		GYRO_ERR("add gyro driver error.\n");
		return -1;
	}
	if(-1 == bmi160_gyro_init_flag) {
	   return -1;
	}
	GYRO_LOG("bmi160 gyro init ok.\n");
	return 0;
}

static struct gyro_init_info bmi160_gyro_init_info = {
		.name = BMG_DEV_NAME,
		.init = bmi160_gyro_local_init,
		.uninit = bmi160_gyro_remove,
};

static int __init bmg_init(void)
{
	const char *name = "mediatek,bmi160_gyro";
	hw = get_gyro_dts_func(name, hw);
	if(!hw) {
		GYRO_ERR("Gyro device tree configuration error.\n");
		return 0;
	}
	GYRO_LOG("%s: bosch gyroscope driver version: %s\n",
	__func__, BMG_DRIVER_VERSION);
	gyro_driver_add(&bmi160_gyro_init_info);
	return 0;
}

static void __exit bmg_exit(void)
{
	GYRO_FUN();
}

module_init(bmg_init);
module_exit(bmg_exit);

MODULE_LICENSE("GPLv2");
MODULE_DESCRIPTION("BMG I2C Driver");
MODULE_AUTHOR("xiaogang.fan@cn.bosch.com");
MODULE_VERSION(BMG_DRIVER_VERSION);
