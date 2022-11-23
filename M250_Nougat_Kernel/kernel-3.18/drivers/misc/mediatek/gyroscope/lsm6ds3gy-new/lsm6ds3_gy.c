/* ST LSM6DS3 Accelerometer and Gyroscope sensor driver
 *
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
 */

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
//#include <linux/earlysuspend.h>
#include <linux/platform_device.h>

#include <hwmsensor.h>
#include "lsm6ds3_gy.h"

#define POWER_NONE_MACRO -1

#define LSM6DS3_GYRO_NEW_ARCH		//kk and L compatialbe

#define LSM6DS3_VIRTUAL_SENSORS

#define CALIBRATION_TO_FILE
#ifdef CALIBRATION_TO_FILE
#include <linux/syscalls.h>
#include <linux/fs.h>
#endif

#ifdef LSM6DS3_GYRO_NEW_ARCH
#include <gyroscope.h>
#endif

#ifdef LSM6DS3_VIRTUAL_SENSORS
#include <../../rotationvector/rotationvector.h>
#endif


/*---------------------------------------------------------------------------*/
#define DEBUG 1
#define DEBUG_ST 1

/*----------------------------------------------------------------------------*/
#define CONFIG_LSM6DS3_LOWPASS   /*apply low pass filter on output*/       
/*----------------------------------------------------------------------------*/
#define LSM6DS3_AXIS_X          0
#define LSM6DS3_AXIS_Y          1
#define LSM6DS3_AXIS_Z          2

#define LSM6DS3_GYRO_AXES_NUM       3 
#define LSM6DS3_GYRO_DATA_LEN       6   
#define LSM6DS3_GYRO_DEV_NAME        "lsm6ds3_gyro"

#ifdef LSM6DS3_VIRTUAL_SENSORS
#define LSM6DS3_ROTATIONVECTOR_DEV_NAME "lsm6ds3_rotationvector"
#define CONVERT_RV_DIV	1000000

static DECLARE_WAIT_QUEUE_HEAD(open_wq);
static bool enable_status_rv;
static bool status_change = false;
#endif

/*----------------------------------------------------------------------------*/
static const struct i2c_device_id lsm6ds3_gyro_i2c_id[] = {{LSM6DS3_GYRO_DEV_NAME,0},{}};
static struct i2c_board_info __initdata i2c_lsm6ds3_gyro={ I2C_BOARD_INFO(LSM6DS3_GYRO_DEV_NAME, 0xD4>>1)}; //0xD4>>1 is right address

/*----------------------------------------------------------------------------*/
static int lsm6ds3_gyro_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int lsm6ds3_gyro_i2c_remove(struct i2c_client *client);
static int LSM6DS3_gyro_init_client(struct i2c_client *client, bool enable);

#ifndef CONFIG_HAS_EARLYSUSPEND
static int lsm6ds3_gyro_resume(struct i2c_client *client);
static int lsm6ds3_gyro_suspend(struct i2c_client *client, pm_message_t msg);
#endif

struct gyro_hw lsm6ds3_gyro_cust;
static struct gyro_hw *gy_hw = &lsm6ds3_gyro_cust;

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
	int gyrodata[LSM6DS3_GYRO_AXES_NUM+1];

	int new_bias_x;
	int new_bias_y;
	int new_bias_z;

	/*update check valirable*/
	int update_criteria_x;
	int update_criteria_y;
	int update_criteria_z;
	int update_criteria_count;
	int update_gyrodata[LSM6DS3_GYRO_AXES_NUM+1]; /*these are only for checking stable or not */
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
#endif

struct gyro_hw* lsm6ds3_get_cust_gyro_hw(void)
{
	return &lsm6ds3_gyro_cust;
}

static s16 cali_old[LSM6DS3_GYRO_AXES_NUM+1];
/*----------------------------------------------------------------------------*/
typedef enum {
    ADX_TRC_FILTER  = 0x01,
    ADX_TRC_RAWDATA = 0x02,
    ADX_TRC_IOCTL   = 0x04,
    ADX_TRC_CALI	= 0X08,
    ADX_TRC_INFO	= 0X10,
} ADX_TRC;
/*----------------------------------------------------------------------------*/
typedef enum {
    GYRO_TRC_FILTER  = 0x01,
    GYRO_TRC_RAWDATA = 0x02,
    GYRO_TRC_IOCTL   = 0x04,
    GYRO_TRC_CALI	= 0X08,
    GYRO_TRC_INFO	= 0X10,
    GYRO_TRC_DATA	= 0X20,
} GYRO_TRC;
/*----------------------------------------------------------------------------*/
struct scale_factor{
    u8  whole;
    u8  fraction;
};
/*----------------------------------------------------------------------------*/
struct data_resolution {
    struct scale_factor scalefactor;
    int                 sensitivity;
};
/*----------------------------------------------------------------------------*/
#define C_MAX_FIR_LENGTH (32)
/*----------------------------------------------------------------------------*/

struct gyro_data_filter {
    s16 raw[C_MAX_FIR_LENGTH][LSM6DS3_GYRO_AXES_NUM];
    int sum[LSM6DS3_GYRO_AXES_NUM];
    int num;
    int idx;
};
/*----------------------------------------------------------------------------*/
struct lsm6ds3_gyro_i2c_data {
    struct i2c_client *client;
    struct gyro_hw *hw;
    struct hwmsen_convert   cvt;
    
    /*misc*/
    //struct data_resolution *reso;
    atomic_t                trace;
    atomic_t                suspend;
    atomic_t                selftest;
    atomic_t				filter;
    atomic_t				fast_calib_rslt;
    s16                     cali_sw[LSM6DS3_GYRO_AXES_NUM+1];
#ifdef SUPPORT_DYNAMIC_UPDATE_GYRO_BIAS
    s16                     cali_data[LSM6DS3_GYRO_AXES_NUM+1];
#endif

    /*data*/
	
    s16                     offset[LSM6DS3_GYRO_AXES_NUM+1];  /*+1: for 4-byte alignment*/
    int                     data[LSM6DS3_GYRO_AXES_NUM+1];
	int 					sensitivity;
#ifdef LSM6DS3_VIRTUAL_SENSORS
    int           quat[4];
#endif

#if defined(CONFIG_LSM6DS3_LOWPASS)
    atomic_t                firlen;
    atomic_t                fir_en;
    struct gyro_data_filter      fir;
#endif 
    /*early suspend*/
#if defined(CONFIG_HAS_EARLYSUSPEND)
    struct early_suspend    early_drv;
#endif     
};
/*----------------------------------------------------------------------------*/
static struct i2c_driver lsm6ds3_gyro_i2c_driver = {
    .driver = {
        .owner          = THIS_MODULE,
        .name           = LSM6DS3_GYRO_DEV_NAME,
    },
	.probe      		= lsm6ds3_gyro_i2c_probe,
	.remove    			= lsm6ds3_gyro_i2c_remove,

#if !defined(CONFIG_HAS_EARLYSUSPEND)    
    .suspend            = lsm6ds3_gyro_suspend,
    .resume             = lsm6ds3_gyro_resume,
#endif
	.id_table = lsm6ds3_gyro_i2c_id,
};
#ifdef LSM6DS3_GYRO_NEW_ARCH
static int lsm6ds3_gyro_local_init(void);
static int lsm6ds3_gyro_local_uninit(void);
static int lsm6ds3_gyro_init_flag = -1;
static struct gyro_init_info  lsm6ds3_gyro_init_info =
    {
        .name   = LSM6DS3_GYRO_DEV_NAME,
        .init   = lsm6ds3_gyro_local_init,
        .uninit = lsm6ds3_gyro_local_uninit,
    };
#endif

#ifdef LSM6DS3_VIRTUAL_SENSORS
static int lsm6ds3_rotationvector_local_init(void);
static int lsm6ds3_rotationvector_local_uninit(void);
struct rotationvector_init_info lsm6ds3_rotationvector_info = {
	.name = LSM6DS3_ROTATIONVECTOR_DEV_NAME,
	.init = lsm6ds3_rotationvector_local_init,
	.uninit = lsm6ds3_rotationvector_local_uninit,
};
#endif


/*----------------------------------------------------------------------------*/
static struct i2c_client *lsm6ds3_i2c_client = NULL;

#ifndef LSM6DS3_GYRO_NEW_ARCH
static struct platform_driver lsm6ds3_driver;
#endif

static struct lsm6ds3_gyro_i2c_data *obj_i2c_data = NULL;
static bool sensor_power = false;
static bool enable_status = false;

/*----------------------------------------------------------------------------*/
#define GYRO_TAG                  "[Gyroscope] "


#define GYRO_FUN(f)               printk(KERN_INFO GYRO_TAG"%s\n", __FUNCTION__)
#define GYRO_ERR(fmt, args...)    printk(KERN_ERR GYRO_TAG"%s %d : "fmt, __FUNCTION__, __LINE__, ##args)
#define GYRO_LOG(fmt, args...)    printk(KERN_INFO GYRO_TAG fmt, ##args)
#define GYRO_DEBUG(fmt, args...)	pr_debug(GYRO_TAG fmt, ##args)


/*----------------------------------------------------------------------------*/

static void LSM6DS3_dumpReg(struct i2c_client *client)
{
  int i=0;
  u8 addr = 0x10;
  u8 regdata=0;
  for(i=0; i<25 ; i++)
  {
    //dump all
    hwmsen_read_byte(client,addr,&regdata);
	HWM_LOG("Reg addr=%x regdata=%x\n",addr,regdata);
	addr++;	
  }
}

/*--------------------gyroscopy power control function----------------------------------*/
static void LSM6DS3_power(struct gyro_hw *hw, unsigned int on) 
{
	static unsigned int power_on = 0;
#if 0
	if(hw->power_id != POWER_NONE_MACRO)		// have externel LDO
	{        
		GYRO_LOG("power %s\n", on ? "on" : "off");
		if(power_on == on)	// power status not change
		{
			GYRO_LOG("ignore power control: %d\n", on);
		}
		else if(on)	// power on
		{
			if(!hwPowerOn(hw->power_id, hw->power_vol, "LSM6DS3"))
			{
				GYRO_ERR("power on fails!!\n");
			}
		}
		else	// power off
		{
			if (!hwPowerDown(hw->power_id, "LSM6DS3"))
			{
				GYRO_ERR("power off fail!!\n");
			}			  
		}
	}
#endif
	power_on = on;    
}
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static int LSM6DS3_gyro_write_rel_calibration(struct lsm6ds3_gyro_i2c_data *obj, int dat[LSM6DS3_GYRO_AXES_NUM])
{
    obj->cali_sw[LSM6DS3_AXIS_X] = obj->cvt.sign[LSM6DS3_AXIS_X]*dat[obj->cvt.map[LSM6DS3_AXIS_X]];
    obj->cali_sw[LSM6DS3_AXIS_Y] = obj->cvt.sign[LSM6DS3_AXIS_Y]*dat[obj->cvt.map[LSM6DS3_AXIS_Y]];
    obj->cali_sw[LSM6DS3_AXIS_Z] = obj->cvt.sign[LSM6DS3_AXIS_Z]*dat[obj->cvt.map[LSM6DS3_AXIS_Z]];
#if DEBUG		
		if(atomic_read(&obj->trace) & GYRO_TRC_CALI)
		{
			GYRO_LOG("test  (%5d, %5d, %5d) ->(%5d, %5d, %5d)->(%5d, %5d, %5d))\n", 
				obj->cvt.sign[LSM6DS3_AXIS_X],obj->cvt.sign[LSM6DS3_AXIS_Y],obj->cvt.sign[LSM6DS3_AXIS_Z],
				dat[LSM6DS3_AXIS_X], dat[LSM6DS3_AXIS_Y], dat[LSM6DS3_AXIS_Z],
				obj->cvt.map[LSM6DS3_AXIS_X],obj->cvt.map[LSM6DS3_AXIS_Y],obj->cvt.map[LSM6DS3_AXIS_Z]);
			GYRO_LOG("write gyro calibration data  (%5d, %5d, %5d)\n", 
				obj->cali_sw[LSM6DS3_AXIS_X],obj->cali_sw[LSM6DS3_AXIS_Y],obj->cali_sw[LSM6DS3_AXIS_Z]);
		}
#endif
    return 0;
}

/*----------------------------------------------------------------------------*/
static int LSM6DS3_gyro_ResetCalibration(struct i2c_client *client)
{
	struct lsm6ds3_gyro_i2c_data *obj = i2c_get_clientdata(client);	

	memset(obj->cali_sw, 0x00, sizeof(obj->cali_sw));
	return 0;    
}

/*----------------------------------------------------------------------------*/
static int LSM6DS3_gyro_ReadCalibration(int* cal_read, char* fname)
{
   int fd;
   int i;
   int res;
   mm_segment_t old_fs = get_fs();
   char temp_str[5];

   set_fs(KERNEL_DS);

   fd = sys_open(fname, O_RDONLY, 0);
	if(fd< 0){
         GYRO_ERR("File Open Error \n");
	     sys_close(fd);
		 return -EINVAL;
		}
   for(i=0;i<6;i++)
   {
	   memset(temp_str,0x00,sizeof(temp_str));
	   res = sys_read(fd, temp_str, sizeof(temp_str));
	   if(res<0){
		   GYRO_ERR("Read Error \n");
		   sys_close(fd);
		   return -EINVAL;
	   }
	   sscanf(temp_str,"%d",&cal_read[i]);
	   GYRO_LOG("lsm6ds3_gyro_calibration_read : cal_read[%d]=%d\n",i,cal_read[i]);
   }
   sys_close(fd);
   set_fs(old_fs);
   GYRO_LOG("lsm6ds3_gyro_calibration_read Done.\n");
   cali_old[0] = cal_read[3];
   cali_old[1] = cal_read[4];
   cali_old[2] = cal_read[5];
   GYRO_LOG("data : %d %d %d / %d %d %d\n",cal_read[0],cal_read[1],cal_read[2],cali_old[0],cali_old[1],cali_old[2]);

    return 0;
}

/*----------------------------------------------------------------------------*/
static int LSM6DS3_gyro_WriteCalibration(int *cal, char* fname)
{
	int err = 0;
	int cali[LSM6DS3_GYRO_AXES_NUM];


	int fd;
   int i;
   int res;
   mm_segment_t old_fs = get_fs();
   char temp_str[5];

   set_fs(KERNEL_DS);

   fd = sys_open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0666);
   if(fd< 0){
	   GYRO_ERR("File Open Error (%d)\n",fd);
	   sys_close(fd);
	   return -EINVAL;
   }
   for(i=0;i<6;i++)
   {
	   memset(temp_str,0x00,sizeof(temp_str));
	   sprintf(temp_str, "%d", cal[i]);
	   res = sys_write(fd,temp_str, sizeof(temp_str));

	   if(res<0) {
		   GYRO_ERR("Write Error \n");
		   sys_close(fd);
		   return -EINVAL;
	   }
   }
   sys_fsync(fd);
   sys_close(fd);

   sys_chmod(fname, 0644);
   set_fs(old_fs);

   GYRO_LOG("lsm6ds3_gyro_calibration_save Done.\n");

   cali_old[0] = cal[3];
   cali_old[1] = cal[4];
   cali_old[2] = cal[5];
   GYRO_LOG("data : %d %d %d / %d %d %d\n",cal[0],cal[1],cal[2],cal[3],cal[4],cal[5]);
	return err;
}
/*----------------------------------------------------------------------------*/
static int make_cal_data_file(void)
{
   int fd;
   int i;
   int res;
   char *fname = "/persist-lg/sensor/gyro_cal_data.txt";
   mm_segment_t old_fs = get_fs();
   char temp_str[5];
   int cal_misc[6]={0,};

   set_fs(KERNEL_DS);

   fd = sys_open(fname, O_RDONLY, 0); /* Cal file exist check */

   if(fd==-2)
   GYRO_LOG("Open Cal File Error. Need to make file (%d)\n",fd);

   if(fd< 0){
	   fd = sys_open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	   if(fd< 0){
		   GYRO_ERR("Open or Make Cal File Error (%d)\n",fd);
		   sys_close(fd);
		   return -EINVAL;
	   }

	   for(i=0;i<6;i++)
	   {
		   memset(temp_str,0x00,sizeof(temp_str));
		   sprintf(temp_str, "%d", cal_misc[i]);
		   res = sys_write(fd,temp_str, sizeof(temp_str));

		   if(res<0) {
			   GYRO_ERR("Write Cal Error \n");
			   sys_close(fd);
			   return -EINVAL;
		   }
	   }
	   GYRO_LOG("make_gyro_cal_data_file Done.\n");
   }
   else
	   GYRO_LOG("Sensor Cal File exist.\n");

   sys_fsync(fd);
   sys_close(fd);

   sys_chmod(fname, 0644);
   set_fs(old_fs);
   return LSM6DS3_SUCCESS;
}
/*----------------------------------------------------------------------------*/
static int LSM6DS3_CheckDeviceID(struct i2c_client *client)
{
	u8 databuf[10];    
	int res = 0;

	memset(databuf, 0, sizeof(u8)*10);    
	databuf[0] = LSM6DS3_FIXED_DEVID;    

	res = hwmsen_read_byte(client,LSM6DS3_WHO_AM_I,databuf);
    GYRO_LOG(" LSM6DS3  id %x!\n",databuf[0]);
	if(databuf[0]!=LSM6DS3_FIXED_DEVID)
	{
		return LSM6DS3_ERR_IDENTIFICATION;
	}

	if (res < 0)
	{
		return LSM6DS3_ERR_I2C;
	}

  /*
    addtional chip settings
   */
	res = hwmsen_write_byte(client, 0x00, 0x40);
	if (res)
    return res;

	res = hwmsen_read_byte(client, 0x63, databuf);
	if (res)
    return res;

	databuf[0] |= 0x10;
	res = hwmsen_write_byte(client, 0x63, databuf[0]);
	if (res)
    return res;

	res = hwmsen_write_byte(client, 0x00, 0x80);
	if (res)
    return res;

	res = hwmsen_write_byte(client, 0x02, 0x02);
	if (res)
    return res;

	res = hwmsen_write_byte(client, 0x00, 0x00);
	if (res)
    return res;
	
	return LSM6DS3_SUCCESS;
}

//----------------------------------------------------------------------------//
static int LSM6DS3_gyro_SetPowerMode(struct i2c_client *client, bool enable)
{
	u8 databuf[2] = {0};    
	int res = 0;

	if(enable == sensor_power)
	{
		GYRO_LOG("Sensor power status is newest!\n");
		return LSM6DS3_SUCCESS;
	}

	if(hwmsen_read_byte(client, LSM6DS3_CTRL2_G, databuf))
	{
		GYRO_ERR("read lsm6ds3 power ctl register err!\n");
		return LSM6DS3_ERR_I2C;
	}


	if(true == enable)
	{
		databuf[0] &= ~LSM6DS3_GYRO_ODR_MASK;//clear lsm6ds3 gyro ODR bits
		databuf[0] |= LSM6DS3_GYRO_ODR_104HZ; //default set 100HZ for LSM6DS3 gyro

		/*reset full scale*/
		databuf[0] &= ~LSM6DS3_GYRO_RANGE_MASK;//clear
		databuf[0] |= LSM6DS3_GYRO_RANGE_2000DPS;
	}
	else
	{
		// do nothing
		databuf[0] &= ~LSM6DS3_GYRO_ODR_MASK;//clear lsm6ds3 gyro ODR bits
		databuf[0] |= LSM6DS3_GYRO_ODR_POWER_DOWN; //POWER DOWN
	}
	databuf[1] = databuf[0];
	databuf[0] = LSM6DS3_CTRL2_G;
	res = i2c_master_send(client, databuf, 0x2);
	if(res <= 0)
	{
		GYRO_LOG("LSM6DS3 set power mode: ODR 100hz failed!\n");
		return LSM6DS3_ERR_I2C;
	}	
	else
	{
		GYRO_LOG("set LSM6DS3 gyro power mode:ODR 100HZ ok %d!\n", enable);
	}


	sensor_power = enable;
	
	return LSM6DS3_SUCCESS;    
}

static int LSM6DS3_Set_RegInc(struct i2c_client *client, bool inc)
{
	u8 databuf[2] = {0};    
	int res = 0;
	//GYRO_FUN();     
	
	if(hwmsen_read_byte(client, LSM6DS3_CTRL3_C, databuf))
	{
		GYRO_ERR("read LSM6DS3_CTRL1_XL err!\n");
		return LSM6DS3_ERR_I2C;
	}
	else
	{
		GYRO_LOG("read  LSM6DS3_CTRL1_XL register: 0x%x\n", databuf[0]);
	}
	if(inc)
	{
		databuf[0] |= (LSM6DS3_CTRL3_C_IFINC | LSM6DS3_CTRL3_C_BDU);
		
		databuf[1] = databuf[0];
		databuf[0] = LSM6DS3_CTRL3_C; 
		
		
		res = i2c_master_send(client, databuf, 0x2);
		if(res <= 0)
		{
			GYRO_ERR("write full scale register err!\n");
			return LSM6DS3_ERR_I2C;
		}
	}
	return LSM6DS3_SUCCESS;    
}

static int LSM6DS3_gyro_SetFullScale(struct i2c_client *client, u8 gyro_fs)
{
	u8 databuf[2] = {0};    
	int res = 0;
	GYRO_FUN();     
	
	if(hwmsen_read_byte(client, LSM6DS3_CTRL2_G, databuf))
	{
		GYRO_ERR("read LSM6DS3_CTRL2_G err!\n");
		return LSM6DS3_ERR_I2C;
	}
	else
	{
		GYRO_LOG("read  LSM6DS3_CTRL2_G register: 0x%x\n", databuf[0]);
	}

	databuf[0] &= ~LSM6DS3_GYRO_RANGE_MASK;//clear 
	databuf[0] |= gyro_fs;
	
	databuf[1] = databuf[0];
	databuf[0] = LSM6DS3_CTRL2_G; 
	
	
	res = i2c_master_send(client, databuf, 0x2);
	if(res <= 0)
	{
		GYRO_ERR("write full scale register err!\n");
		return LSM6DS3_ERR_I2C;
	}
	return LSM6DS3_SUCCESS;    
}

/*----------------------------------------------------------------------------*/

// set the gyro sample rate
static int LSM6DS3_gyro_SetSampleRate(struct i2c_client *client, u8 sample_rate)
{
	u8 databuf[2] = {0}; 
	int res = 0;
	GYRO_FUN();    

	res = LSM6DS3_gyro_SetPowerMode(client, true);	//set Sample Rate will enable power and should changed power status
	if(res != LSM6DS3_SUCCESS)
	{
		return res;
	}

	if(hwmsen_read_byte(client, LSM6DS3_CTRL2_G, databuf))
	{
		GYRO_ERR("read gyro data format register err!\n");
		return LSM6DS3_ERR_I2C;
	}
	else
	{
		GYRO_LOG("read  gyro data format register: 0x%x\n", databuf[0]);
	}

	databuf[0] &= ~LSM6DS3_GYRO_ODR_MASK;//clear 
	databuf[0] |= sample_rate;
	
	databuf[1] = databuf[0];
	databuf[0] = LSM6DS3_CTRL2_G; 
	
	
	res = i2c_master_send(client, databuf, 0x2);
	if(res <= 0)
	{
		GYRO_ERR("write sample rate register err!\n");
		return LSM6DS3_ERR_I2C;
	}
	
	return LSM6DS3_SUCCESS;    
}

/*----------------------------------------------------------------------------*/
#ifdef SUPPORT_DYNAMIC_UPDATE_GYRO_BIAS

static void LSM6DS3_ExcuteDynamicGyroBias(struct dynamic_gyro_bias* d_gyro,     int* data, s16* cali)
{
	if(!d_gyro->dynamic_update_gyro_bias){
		int x_diff = (abs(data[LSM6DS3_AXIS_X]) - abs(d_gyro->gyrodata[LSM6DS3_AXIS_X]));
		int y_diff = (abs(data[LSM6DS3_AXIS_Y]) - abs(d_gyro->gyrodata[LSM6DS3_AXIS_Y]));
		int z_diff = (abs(data[LSM6DS3_AXIS_Z]) - abs(d_gyro->gyrodata[LSM6DS3_AXIS_Z]));

		if((abs(x_diff) <= d_gyro->stable_criteria_x) &&
			(abs(y_diff) <= d_gyro->stable_criteria_y) &&
			(abs(z_diff) <= d_gyro->stable_criteria_z)){

			if(d_gyro->stable_check_count == d_gyro->stable_criteria_count){
				cali[LSM6DS3_AXIS_X] = (s16)((-1) * (d_gyro->new_bias_x / d_gyro->stable_criteria_count));
				cali[LSM6DS3_AXIS_Y] = (s16)((-1) * (d_gyro->new_bias_y / d_gyro->stable_criteria_count));
				cali[LSM6DS3_AXIS_Z] = (s16)((-1) * (d_gyro->new_bias_z / d_gyro->stable_criteria_count));
				
				GYRO_LOG("d_gyro Excute Gyro bias completed : gyro sum [%d, %d, %d] \n", d_gyro->new_bias_x, d_gyro->new_bias_y, d_gyro->new_bias_z);
				d_gyro->stable_check_count = 0;
				d_gyro->dynamic_update_gyro_bias = 1;
				GYRO_LOG("d_gyro Excute Gyro bias completed : updated bias[%d %d %d]\n", cali[0],cali[1], cali[2]);
				return;
			}
			d_gyro->stable_check_count++;
			d_gyro->new_bias_x += data[LSM6DS3_AXIS_X];
			d_gyro->new_bias_y += data[LSM6DS3_AXIS_Y];
			d_gyro->new_bias_z += data[LSM6DS3_AXIS_Z];
		}else{
			// It should be checked with 50 "conitunus" data.
			d_gyro->stable_check_count = 0;
			d_gyro->new_bias_x = 0;
			d_gyro->new_bias_y = 0;
			d_gyro->new_bias_z = 0;
		}
		d_gyro->gyrodata[LSM6DS3_AXIS_X] = data[LSM6DS3_AXIS_X];
		d_gyro->gyrodata[LSM6DS3_AXIS_Y] = data[LSM6DS3_AXIS_Y];
		d_gyro->gyrodata[LSM6DS3_AXIS_Z] = data[LSM6DS3_AXIS_Z];

	}
}


static int LSM6DS3_CheckStableToUpdateBias(struct dynamic_gyro_bias* d_gyro, int* data, int criteria)
{
	if(d_gyro->dynamic_update_gyro_bias){
		int x_diff = (abs(data[LSM6DS3_AXIS_X]) - abs(d_gyro->update_gyrodata[LSM6DS3_AXIS_X]));
		int y_diff = (abs(data[LSM6DS3_AXIS_Y]) - abs(d_gyro->update_gyrodata[LSM6DS3_AXIS_Y]));
		int z_diff = (abs(data[LSM6DS3_AXIS_Z]) - abs(d_gyro->update_gyrodata[LSM6DS3_AXIS_Z]));

		if((abs(x_diff) <= d_gyro->stable_criteria_x) &&
			(abs(y_diff) <= d_gyro->stable_criteria_y) &&
			(abs(z_diff) <= d_gyro->stable_criteria_z)){
			if(d_gyro->stable_check_count == criteria){
				d_gyro->stable_check_count = 0;
				return 1;
			}
			d_gyro->stable_check_count++;
		}else{
			d_gyro->stable_check_count = 0;
		}
		d_gyro->update_gyrodata[LSM6DS3_AXIS_X] = data[LSM6DS3_AXIS_X];
		d_gyro->update_gyrodata[LSM6DS3_AXIS_Y] = data[LSM6DS3_AXIS_Y];
		d_gyro->update_gyrodata[LSM6DS3_AXIS_Z] = data[LSM6DS3_AXIS_Z];

	}
	return 0;
}


static void LSM6DS3_UpdateDynamicGyroBias(struct dynamic_gyro_bias* d_gyro, int* data)
{
	if((d_gyro->is_stable) && d_gyro->dynamic_update_gyro_bias){
		if( (abs(data[LSM6DS3_AXIS_X]) > d_gyro->update_criteria_x) ||
			(abs(data[LSM6DS3_AXIS_Y]) > d_gyro->update_criteria_y) ||
			(abs(data[LSM6DS3_AXIS_Z]) > d_gyro->update_criteria_z)){
					d_gyro->dynamic_update_gyro_bias = 0;
					d_gyro->new_bias_x = 0;
					d_gyro->new_bias_y = 0;
					d_gyro->new_bias_z = 0;
					d_gyro->is_stable = 0;
					GYRO_LOG("d_gyro LSM6DS3_UpdateDynamicGyroBias() -> Excute d_gyro\n");
					return;
		}
	}
}
#endif

/*----------------------------------------------------------------------------*/
static int LSM6DS3_ReadGyroData(struct i2c_client *client, char *buf, int bufsize)
{
	char databuf[6];	
	int data[3];
	int i;
#ifdef SUPPORT_DYNAMIC_UPDATE_GYRO_BIAS
	struct dynamic_gyro_bias* d_gyro = &dynamic_gyro;
#endif
	struct lsm6ds3_gyro_i2c_data *obj = i2c_get_clientdata(client);  
	
	if(atomic_read(&obj->suspend)){
		GYRO_ERR("lsm6ds3 suspend status!!");
		return -1;
	}

	if(sensor_power == false)
	{
		LSM6DS3_gyro_SetPowerMode(client, true);
		msleep(100);
	}

	if(hwmsen_read_block(client, LSM6DS3_OUTX_L_G, databuf, 6))
	{
		GYRO_ERR("LSM6DS3 read gyroscope data  error\n");
		return -2;
	}
	else
	{
		obj->data[LSM6DS3_AXIS_X] = (s16)((databuf[LSM6DS3_AXIS_X*2+1] << 8) | (databuf[LSM6DS3_AXIS_X*2]));
		obj->data[LSM6DS3_AXIS_Y] = (s16)((databuf[LSM6DS3_AXIS_Y*2+1] << 8) | (databuf[LSM6DS3_AXIS_Y*2]));
		obj->data[LSM6DS3_AXIS_Z] = (s16)((databuf[LSM6DS3_AXIS_Z*2+1] << 8) | (databuf[LSM6DS3_AXIS_Z*2]));
		
#if DEBUG		
		if(atomic_read(&obj->trace) & GYRO_TRC_RAWDATA)
		{
			GYRO_LOG("read gyro register: %x, %x, %x, %x, %x, %x",
				databuf[0], databuf[1], databuf[2], databuf[3], databuf[4], databuf[5]);
			GYRO_LOG("get gyro raw data (0x%08X, 0x%08X, 0x%08X) -> (%5d, %5d, %5d)\n", 
				obj->data[LSM6DS3_AXIS_X],obj->data[LSM6DS3_AXIS_Y],obj->data[LSM6DS3_AXIS_Z],
				obj->data[LSM6DS3_AXIS_X],obj->data[LSM6DS3_AXIS_Y],obj->data[LSM6DS3_AXIS_Z]);
			GYRO_LOG("get gyro cali data (%5d, %5d, %5d)\n", 
				obj->cali_sw[LSM6DS3_AXIS_X],obj->cali_sw[LSM6DS3_AXIS_Y],obj->cali_sw[LSM6DS3_AXIS_Z]);
		}
#endif	
#if 1
	#if 0
		obj->data[LSM6DS3_AXIS_X] = (long)(obj->data[LSM6DS3_AXIS_X]) * LSM6DS3_GYRO_SENSITIVITY_2000DPS*3142/(180*1000*1000);
		obj->data[LSM6DS3_AXIS_Y] = (long)(obj->data[LSM6DS3_AXIS_Y]) * LSM6DS3_GYRO_SENSITIVITY_2000DPS*3142/(180*1000*1000);
		obj->data[LSM6DS3_AXIS_Z] = (long)(obj->data[LSM6DS3_AXIS_Z]) * LSM6DS3_GYRO_SENSITIVITY_2000DPS*3142/(180*1000*1000); 
	#endif
			/*report degree/s */
		obj->data[LSM6DS3_AXIS_X] = (long)(obj->data[LSM6DS3_AXIS_X])*(LSM6DS3_GYRO_SENSITIVITY_2000DPS/1000)*131/1000;
		obj->data[LSM6DS3_AXIS_Y] = (long)(obj->data[LSM6DS3_AXIS_Y])*(LSM6DS3_GYRO_SENSITIVITY_2000DPS/1000)*131/1000;
		obj->data[LSM6DS3_AXIS_Z] = (long)(obj->data[LSM6DS3_AXIS_Z])*(LSM6DS3_GYRO_SENSITIVITY_2000DPS/1000)*131/1000;

#ifdef SUPPORT_DYNAMIC_UPDATE_GYRO_BIAS
		LSM6DS3_ExcuteDynamicGyroBias(d_gyro, obj->data, obj->cali_sw);
		d_gyro->is_stable = LSM6DS3_CheckStableToUpdateBias(d_gyro, obj->data, d_gyro->update_criteria_count);

#endif
		obj->data[LSM6DS3_AXIS_X] += obj->cali_sw[LSM6DS3_AXIS_X];
		obj->data[LSM6DS3_AXIS_Y] += obj->cali_sw[LSM6DS3_AXIS_Y];
		obj->data[LSM6DS3_AXIS_Z] += obj->cali_sw[LSM6DS3_AXIS_Z];

#ifdef SUPPORT_DYNAMIC_UPDATE_GYRO_BIAS
		LSM6DS3_UpdateDynamicGyroBias(d_gyro, obj->data);
#endif

		/*remap coordinate*/
		data[obj->cvt.map[LSM6DS3_AXIS_X]] = obj->cvt.sign[LSM6DS3_AXIS_X]*obj->data[LSM6DS3_AXIS_X];
		data[obj->cvt.map[LSM6DS3_AXIS_Y]] = obj->cvt.sign[LSM6DS3_AXIS_Y]*obj->data[LSM6DS3_AXIS_Y];
		data[obj->cvt.map[LSM6DS3_AXIS_Z]] = obj->cvt.sign[LSM6DS3_AXIS_Z]*obj->data[LSM6DS3_AXIS_Z];
#else
		data[LSM6DS3_AXIS_X] = (s64)(data[LSM6DS3_AXIS_X]) * LSM6DS3_GYRO_SENSITIVITY_2000DPS*3142/(180*1000*1000);
		data[LSM6DS3_AXIS_Y] = (s64)(data[LSM6DS3_AXIS_Y]) * LSM6DS3_GYRO_SENSITIVITY_2000DPS*3142/(180*1000*1000);
		data[LSM6DS3_AXIS_Z] = (s64)(data[LSM6DS3_AXIS_Z]) * LSM6DS3_GYRO_SENSITIVITY_2000DPS*3142/(180*1000*1000); 
#endif
	}

	sprintf(buf, "%x %x %x", data[LSM6DS3_AXIS_X],data[LSM6DS3_AXIS_Y],data[LSM6DS3_AXIS_Z]);

#if DEBUG		
	if(atomic_read(&obj->trace) & GYRO_TRC_DATA)
	{
		GYRO_LOG("get gyro data packet:[%d %d %d]\n", data[0], data[1], data[2]);
	}
#endif
	
	return 0;
	
}


static int LSM6DS3_ReadGyroRawData(struct i2c_client *client, s16 data[LSM6DS3_GYRO_AXES_NUM])
{
	int err = 0;
	char databuf[6] = {0};

	if(NULL == client)
	{
		err = -EINVAL;
	}
	else
	{
		if(hwmsen_read_block(client, LSM6DS3_OUTX_L_G, databuf, 6))
		{
			GYRO_ERR("LSM6DS3 read gyro data  error\n");
			return -2;
		}
		else
		{
			data[LSM6DS3_AXIS_X] = (s16)((databuf[LSM6DS3_AXIS_X*2+1] << 8) | (databuf[LSM6DS3_AXIS_X*2]));
			data[LSM6DS3_AXIS_Y] = (s16)((databuf[LSM6DS3_AXIS_Y*2+1] << 8) | (databuf[LSM6DS3_AXIS_Y*2]));
			data[LSM6DS3_AXIS_Z] = (s16)((databuf[LSM6DS3_AXIS_Z*2+1] << 8) | (databuf[LSM6DS3_AXIS_Z*2]));
		}
	}
	return err;
}


/*----------------------------------------------------------------------------*/
static int LSM6DS3_ReadChipInfo(struct i2c_client *client, char *buf, int bufsize)
{
	u8 databuf[10];    

	memset(databuf, 0, sizeof(u8)*10);

	if((NULL == buf)||(bufsize<=30))
	{
		return -1;
	}
	
	if(NULL == client)
	{
		*buf = 0;
		return -2;
	}

	sprintf(buf, "LSM6DS3 Chip");
	return 0;
}


/*----------------------------------------------------------------------------*/
static ssize_t show_chipinfo_value(struct device_driver *ddri, char *buf)
{
	struct i2c_client *client = lsm6ds3_i2c_client;
	char strbuf[LSM6DS3_BUFSIZE];
	if(NULL == client)
	{
		GYRO_ERR("i2c client is null!!\n");
		return 0;
	}
	
	LSM6DS3_ReadChipInfo(client, strbuf, LSM6DS3_BUFSIZE);
	return snprintf(buf, PAGE_SIZE, "%s\n", strbuf);        
}
/*----------------------------------------------------------------------------*/
static ssize_t show_sensordata_value(struct device_driver *ddri, char *buf)
{
	struct i2c_client *client = lsm6ds3_i2c_client;
	char strbuf[LSM6DS3_BUFSIZE];
	
	if(NULL == client)
	{
		GYRO_ERR("i2c client is null!!\n");
		return 0;
	}
	
	LSM6DS3_ReadGyroData(client, strbuf, LSM6DS3_BUFSIZE);
	
	return snprintf(buf, PAGE_SIZE, "%s\n", strbuf);            
}

/*----------------------------------------------------------------------------*/
static ssize_t show_trace_value(struct device_driver *ddri, char *buf)
{
	ssize_t res;
	struct lsm6ds3_gyro_i2c_data *obj = obj_i2c_data;
	if (obj == NULL)
	{
		GYRO_ERR("i2c_data obj is null!!\n");
		return 0;
	}
	
	res = snprintf(buf, PAGE_SIZE, "0x%04X\n", atomic_read(&obj->trace));     
	return res;    
}
/*----------------------------------------------------------------------------*/
static ssize_t store_trace_value(struct device_driver *ddri, const char *buf, size_t count)
{
	struct lsm6ds3_gyro_i2c_data *obj = obj_i2c_data;
	int trace;
	if (obj == NULL)
	{
		GYRO_ERR("i2c_data obj is null!!\n");
		return count;
	}
	
	if(1 == sscanf(buf, "0x%x", &trace))
	{
		atomic_set(&obj->trace, trace);
	}	
	else
	{
		GYRO_ERR("invalid content: '%s', length = %zu\n", buf, count);
	}
	
	return count;    
}
/*----------------------------------------------------------------------------*/
static ssize_t show_status_value(struct device_driver *ddri, char *buf)
{
	ssize_t len = 0;    
	struct lsm6ds3_gyro_i2c_data *obj = obj_i2c_data;
	if (obj == NULL)
	{
		GYRO_ERR("i2c_data obj is null!!\n");
		return 0;
	}	
	
	if(obj->hw)
	{
		len += snprintf(buf+len, PAGE_SIZE-len, "CUST: %d %d (%d %d)\n", 
	            obj->hw->i2c_num, obj->hw->direction, obj->hw->power_id, obj->hw->power_vol);   
	}
	else
	{
		len += snprintf(buf+len, PAGE_SIZE-len, "CUST: NULL\n");
	}
	return len;    
}
/*----------------------------------------------------------------------------*/

static int lsm6ds3_update_reg(struct i2c_client *client, u8 reg, u8 mask, u8 value)
{
	int ret = 0;

	u8 v;

	ret = hwmsen_read_byte(client, reg, &v);
	if (ret!=0)
		return -1;

	v &= ~((u8) mask);
	v |= value;

	ret = hwmsen_write_byte(client, reg, v);
	if (ret!=0)
		return -1;

	return ret;
}

static int lsm6ds3_gy_hw_selftest_on(const struct i2c_client *client)
{
	int status = LSM6DS3_ERR_STATUS;
	u8 ucTestset_regs[10] =
	    { 0x00, 0x5C, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38 };

	/* Initialize Sensor, turn on sensor, enable P/R/Y */
	/* Set BDU=1, ODR=208Hz, FS=2000dps */

	status =
	    hwmsen_write_block(client, LSM6DS3_CTRL1_XL, ucTestset_regs, 5);
	if (status != LSM6DS3_SUCCESS) {
		return status;
	}
	status =
	    hwmsen_write_block(client, LSM6DS3_CTRL6_C, &ucTestset_regs[5], 5);
	if (status != LSM6DS3_SUCCESS) {
		return status;
	}

	/* Set Bypass Mode */
	status =
	    lsm6ds3_update_reg(client, LSM6DS3_FIFO_CTRL5,
			       LSM6DS3_FIFO_CTRL5_MODE_MSK,
			       LSM6DS3_FIFO_CTRL5_MODE_BYPASS);
	if (status != LSM6DS3_SUCCESS) {
		return status;
	}

	return LSM6DS3_SUCCESS;
}

static int lsm6ds3_gyro_do_hw_selftest(const struct i2c_client *client, s32 NOST[3],
			    s32 ST[3])
{
	int status = LSM6DS3_SUCCESS;
	u8 readBuffer[1] = { 0x00, };
	s16 nOutData[3] = { 0, };
	u8 backup_regs[10];

	s32 i, retry;

	for (i = 0; i < 3; i++) {
		NOST[i] = ST[i] = 0;
	}

	status = hwmsen_read_block(client, LSM6DS3_CTRL1_XL, backup_regs, 5);
	if (status != 0) {
		goto G_HW_SELF_EXIT;
	}
	status = hwmsen_read_block(client, LSM6DS3_CTRL6_C, &backup_regs[5], 5);
	if (status != 0) {
		goto G_HW_SELF_EXIT;
	}


	status = lsm6ds3_gy_hw_selftest_on(client);
	if (status != LSM6DS3_SUCCESS) {
		GYRO_ERR("err!!\n");
		goto G_HW_SELF_EXIT;
	}

	mdelay(200);

	retry = LSM6DS3_DA_RETRY_COUNT;
	do {
		mdelay(5);
		status =
		    hwmsen_read_block(client, LSM6DS3_STATUS_REG, readBuffer,
				      1);
		if (status != LSM6DS3_SUCCESS) {
		    GYRO_ERR("err!!\n");
			goto G_HW_SELF_EXIT;
		}
		retry--;
		if (!retry)
			break;
	} while (!(readBuffer[0] & LSM6DS3_STATUS_REG_GDA));

	status =
	    hwmsen_read_block(client, LSM6DS3_OUTX_L_G, (u8 *) nOutData, 6);
	if (status != LSM6DS3_SUCCESS) {
		goto G_HW_SELF_EXIT;
	}

	for (i = 0; i < 6; i++) {
		retry = LSM6DS3_DA_RETRY_COUNT;
		do {
			mdelay(5);
			status =
			    hwmsen_read_block(client, LSM6DS3_STATUS_REG,
					      readBuffer, 1);
			if (status != LSM6DS3_SUCCESS) {
		        GYRO_ERR("err!!\n");
				goto G_HW_SELF_EXIT;
			}
			retry--;
			if (!retry)
				break;
		} while (!(readBuffer[0] & LSM6DS3_STATUS_REG_GDA));

		status =
		    hwmsen_read_block(client, LSM6DS3_OUTX_L_G, (u8 *) nOutData,
				      6);
		if (status != LSM6DS3_SUCCESS) {
		    GYRO_ERR("err!!\n");
			goto G_HW_SELF_EXIT;
		}
#if DEBUG_ST == 1
		GYRO_LOG("NOST sample[%d] :  %d %d %d\n", i, nOutData[0],
			 nOutData[1], nOutData[2]);
#endif

		if (i > 0) {
			NOST[0] += nOutData[0];
			NOST[1] += nOutData[1];
			NOST[2] += nOutData[2];
		}
	}

	NOST[0] /= 5;
	NOST[1] /= 5;
	NOST[2] /= 5;

	status =
	    lsm6ds3_update_reg(client, LSM6DS3_CTRL5_C, LSM6DS3_ST_G_MASK,
			       LSM6DS3_ST_G_ON_POS);
	if (status != LSM6DS3_SUCCESS) {
		goto G_HW_SELF_EXIT;
	}

	mdelay(200);

	retry = LSM6DS3_DA_RETRY_COUNT;
	do {
		mdelay(5);
		status =
		    hwmsen_read_block(client, LSM6DS3_STATUS_REG, readBuffer,
				      1);
		if (status != LSM6DS3_SUCCESS) {
			goto G_HW_SELF_EXIT;
		}
		retry--;
		if (!retry)
			break;
	} while (!(readBuffer[0] & LSM6DS3_STATUS_REG_GDA));

	status =
	    hwmsen_read_block(client, LSM6DS3_OUTX_L_G, (u8 *) nOutData, 6);
	if (status != LSM6DS3_SUCCESS) {
		goto G_HW_SELF_EXIT;
	}

	for (i = 0; i < 6; i++) {
		retry = LSM6DS3_DA_RETRY_COUNT;
		do {
			mdelay(5);
			status =
			    hwmsen_read_block(client, LSM6DS3_STATUS_REG,
					      readBuffer, 1);
			if (status != LSM6DS3_SUCCESS) {
				goto G_HW_SELF_EXIT;
			}
			retry--;
			if (!retry)
				break;
		} while (!(readBuffer[0] & LSM6DS3_STATUS_REG_GDA));

		status =
		    hwmsen_read_block(client, LSM6DS3_OUTX_L_G, (u8 *) nOutData,
				      6);
		if (status != LSM6DS3_SUCCESS) {
			goto G_HW_SELF_EXIT;
		}
#if DEBUG_ST == 1
		GYRO_LOG("ST sample[%d] :  %d %d %d\n", i, nOutData[0],
			 nOutData[1], nOutData[2]);
#endif

		if (i > 0) {
			ST[0] += nOutData[0];
			ST[1] += nOutData[1];
			ST[2] += nOutData[2];
		}
	}

	ST[0] /= 5;
	ST[1] /= 5;
	ST[2] /= 5;

	status = lsm6ds3_update_reg(client, LSM6DS3_CTRL2_G, 0xff, 0x00);
	if (status != LSM6DS3_SUCCESS) {
		goto G_HW_SELF_EXIT;
	}

	status =
	    lsm6ds3_update_reg(client, LSM6DS3_CTRL5_C, LSM6DS3_ST_G_MASK,
			       LSM6DS3_ST_G_OFF);
	if (status != LSM6DS3_SUCCESS) {
		goto G_HW_SELF_EXIT;
	}

	retry = 0;
	for (i = 0; i < 3; i++) {
		ST[i] = abs(ST[i] - NOST[i]);
		if ((LSM6DS3_MIN_ST_2000DPS > ST[i])
		    || (LSM6DS3_MAX_ST_2000DPS < ST[i])) {
			retry++;
		}
	}

	if (retry > 0) {
		status = LSM6DS3_ERR_STATUS;
	} else {
		status = LSM6DS3_SUCCESS;
	}

G_HW_SELF_EXIT:
	// restore registers
	hwmsen_write_block(client, LSM6DS3_CTRL1_XL, backup_regs, 5);
	hwmsen_write_block(client, LSM6DS3_CTRL6_C, &backup_regs[5], 5);

	mdelay(200);

	return status;
}

static ssize_t show_selftest_value(struct device_driver *ddri, char *buf)
{
	ssize_t res;

	struct lsm6ds3_gyro_i2c_data *priv = obj_i2c_data;
	if (priv == NULL) {
		GYRO_ERR("i2c_data obj is null!!\n");
		return 0;
	}



	res = snprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&priv->selftest));
	return res;
}

static ssize_t store_selftest_value(struct device_driver *ddri, const char *buf, size_t count)
{
	int st_result = 0;
	s32 nost[3] = { 0 }, st[3] = { 0 };

	struct lsm6ds3_gyro_i2c_data *priv = obj_i2c_data;
	if (priv == NULL) {
		GYRO_ERR("i2c_data obj is null!!\n");
		return 0;
	}
	if(1 == sscanf(buf, "%d", &st_result))
	{
		st_result = lsm6ds3_gyro_do_hw_selftest(priv->client, nost, st);
		atomic_set(&priv->selftest, st_result);
	}else
	{
		GYRO_ERR("invalid content: '%s', length = %zu\n", buf, count);
	}

	return count;
}

static int lsm6ds3_do_gy_calibration(struct lsm6ds3_gyro_i2c_data *priv)
{
	int res, i = 0;
	int cali[6];
	int sum[3] = { 0 };
	long cnvt[3];
	s16 data[3] = { 0 }, data_prev[3] = {
	0};
	struct i2c_client *client;
	s16 cali_backup[3];

	if (priv == NULL)
		return -EINVAL;;

	client = priv->client;

	/*backup calibrated data */
	memcpy(cali_backup, priv->cali_sw, sizeof (cali_backup));

	if (sensor_power == false) {
		res = LSM6DS3_gyro_SetPowerMode(client, true);
		if (res) {
			GYRO_ERR("Power on lsm6ds3 error %d!\n", res);
			return LSM6DS3_ERR_SETUP_FAILURE;
		}
		msleep(100);
	}

	res = LSM6DS3_ReadGyroRawData(client, data_prev);
	if (res < 0) {
		GYRO_ERR("I2C error: ret value=%d", res);
		return LSM6DS3_ERR_SETUP_FAILURE;
	}

	for (i = 0; i < LSM6DS3_CALI_DATA_NUM; i++) {
		mdelay(20);
		res = LSM6DS3_ReadGyroRawData(client, data);
		if (res < 0) {
			GYRO_ERR("I2C error: ret value=%d", res);
			return LSM6DS3_ERR_SETUP_FAILURE;
		}
#if DEBUG_ST == 1
		GYRO_LOG("sample[%d] = %d %d %d %d %d %d\n", i,
			 data[0], data[1], data[2], data_prev[0], data_prev[1],
			 data_prev[2]);
#endif

		if ((abs(data[0] - data_prev[0]) >
		     LSM6DS3_SHAKING_DETECT_THRESHOLD)
		    || (abs(data[1] - data_prev[1]) >
			LSM6DS3_SHAKING_DETECT_THRESHOLD)
		    || (abs(data[2] - data_prev[2]) >
			LSM6DS3_SHAKING_DETECT_THRESHOLD)) {
			GYRO_LOG("===============shaking x===============\n");
		} else {
			sum[0] += data[0];
			sum[1] += data[1];
			sum[2] += data[2];

			memcpy(data_prev, data, sizeof (data));
		}

	}

	GYRO_LOG("===============complete shaking x check===============\n");

#if DEBUG_ST == 1
	GYRO_LOG("average :  %d %d %d\n", sum[0] / LSM6DS3_CALI_DATA_NUM,
		 sum[1] / LSM6DS3_CALI_DATA_NUM,
		 sum[2] / LSM6DS3_CALI_DATA_NUM);
#endif

	if ((abs(sum[0]) / LSM6DS3_CALI_DATA_NUM > LSM6DS3_CALIB_THRESH_LSB)
	    || (abs(sum[1]) / LSM6DS3_CALI_DATA_NUM > LSM6DS3_CALIB_THRESH_LSB)
	    || (abs(sum[2]) / LSM6DS3_CALI_DATA_NUM > LSM6DS3_CALIB_THRESH_LSB)) {
		GYRO_ERR("I2C error: ret value=%d", res);
		return LSM6DS3_ERR_SETUP_FAILURE;
	}

	cnvt[0] = -1 * sum[0] / LSM6DS3_CALI_DATA_NUM;
	cnvt[1] = -1 * sum[1] / LSM6DS3_CALI_DATA_NUM;
	cnvt[2] = -1 * sum[2] / LSM6DS3_CALI_DATA_NUM;

	cnvt[0] = cnvt[0] * (LSM6DS3_GYRO_SENSITIVITY_2000DPS/1000) * 131 / 1000;
	cnvt[1] = cnvt[1] * (LSM6DS3_GYRO_SENSITIVITY_2000DPS/1000) * 131 / 1000;
	cnvt[2] = cnvt[2] * (LSM6DS3_GYRO_SENSITIVITY_2000DPS/1000) * 131 / 1000;

	priv->cali_sw[0] = (s16) cnvt[0];
	priv->cali_sw[1] = (s16) cnvt[1];
	priv->cali_sw[2] = (s16) cnvt[2];

#ifdef SUPPORT_DYNAMIC_UPDATE_GYRO_BIAS
	priv->cali_data[0] = (s16) cnvt[0];
	priv->cali_data[1] = (s16) cnvt[1];
	priv->cali_data[2] = (s16) cnvt[2];
#endif

	cali[3] = (int)cali_backup[0];
	cali[4] = (int)cali_backup[1];
	cali[5] = (int)cali_backup[2];

	cali[0] = (int)priv->cali_sw[0];
	cali[1] = (int)priv->cali_sw[1];
	cali[2] = (int)priv->cali_sw[2];

	LSM6DS3_gyro_WriteCalibration(cali, "/persist-lg/sensor/gyro_cal_data.txt");
#if DEBUG_ST == 1
	GYRO_LOG("updated cali_sw :  %d %d %d\n", priv->cali_sw[0],
		 priv->cali_sw[1], priv->cali_sw[2]);
#endif

	return LSM6DS3_SUCCESS;
}

static ssize_t store_run_fast_calibration_value(struct device_driver *ddri, const char *buf,
				 size_t count)
{
	struct lsm6ds3_gyro_i2c_data *data = obj_i2c_data;
	int do_calibrate = 0;
	int res = 0;
	if (NULL == data) {
		printk(KERN_ERR "lsm6ds3_gyro_i2c_data is null!!\n");
		return count;
	}

	sscanf(buf, "%d", &do_calibrate);
	res = lsm6ds3_do_gy_calibration(data);
	if (LSM6DS3_SUCCESS == res) {
		atomic_set(&data->fast_calib_rslt, 1);
		GYRO_LOG("calibration LSM6DS3_SUCCESS\n");
	}
	else if (LSM6DS3_ERR_SETUP_FAILURE == res) {
		atomic_set(&data->fast_calib_rslt, 2);
		GYRO_ERR("calibration LSM6DS3_ERR_SETUP_FAILURE!\n");
	} else {
		atomic_set(&data->fast_calib_rslt, 0);
		GYRO_ERR("calibration FAIL!\n");
	}
	return count;
}

static ssize_t show_run_fast_calibration_value(struct device_driver *ddri, char *buf)
{
	ssize_t res;
	int st_result = 0;

	struct lsm6ds3_gyro_i2c_data *data = obj_i2c_data;
	if (data == NULL) {
		GYRO_ERR("i2c_data obj is null!!\n");
		return 0;
	}

	res =
	    snprintf(buf, PAGE_SIZE, "%d\n",
		     atomic_read(&data->fast_calib_rslt));
	return res;
}



static DRIVER_ATTR(chipinfo,             S_IRUGO, show_chipinfo_value,      NULL);
static DRIVER_ATTR(sensordata,           S_IRUGO, show_sensordata_value,    NULL);
static DRIVER_ATTR(trace,      S_IRUGO, show_trace_value,         store_trace_value);
static DRIVER_ATTR(status,               S_IRUGO, show_status_value,        NULL);
static DRIVER_ATTR(selftest,               S_IRUGO | S_IWUSR | S_IWGRP, show_selftest_value,        store_selftest_value);
static DRIVER_ATTR(run_fast_calibration,               S_IRUGO | S_IWUSR | S_IWGRP, show_run_fast_calibration_value,  store_run_fast_calibration_value);

/*----------------------------------------------------------------------------*/
static struct driver_attribute *LSM6DS3_attr_list[] = {
	&driver_attr_chipinfo,     /*chip information*/
	&driver_attr_sensordata,   /*dump sensor data*/	
	&driver_attr_trace,        /*trace log*/
	&driver_attr_status,        
	&driver_attr_selftest,
    &driver_attr_run_fast_calibration,
};
/*----------------------------------------------------------------------------*/
static int lsm6ds3_create_attr(struct device_driver *driver) 
{
	int idx, err = 0;
	int num = (int)(sizeof(LSM6DS3_attr_list)/sizeof(LSM6DS3_attr_list[0]));
	if (driver == NULL)
	{
		return -EINVAL;
	}

	for(idx = 0; idx < num; idx++)
	{
		if(0 != (err = driver_create_file(driver,  LSM6DS3_attr_list[idx])))
		{            
			GYRO_ERR("driver_create_file (%s) = %d\n",  LSM6DS3_attr_list[idx]->attr.name, err);
			break;
		}
	}    
	return err;
}
/*----------------------------------------------------------------------------*/
static int lsm6ds3_delete_attr(struct device_driver *driver)
{
	int idx ,err = 0;
	int num = (int)(sizeof( LSM6DS3_attr_list)/sizeof( LSM6DS3_attr_list[0]));

	if(driver == NULL)
	{
		return -EINVAL;
	}	

	for(idx = 0; idx < num; idx++)
	{
		driver_remove_file(driver,  LSM6DS3_attr_list[idx]);
	}
	return err;
}

/*----------------------------------------------------------------------------*/
static int LSM6DS3_gyro_init_client(struct i2c_client *client, bool enable)
{
	struct lsm6ds3_gyro_i2c_data *obj = i2c_get_clientdata(client);
	int res = 0;
	
	GYRO_LOG("%s lsm6ds3 addr %x!\n", __FUNCTION__, client->addr);
	
	res = LSM6DS3_CheckDeviceID(client);
	if(res != LSM6DS3_SUCCESS)
	{
		return res;
	}
	
	res = LSM6DS3_Set_RegInc(client, true);
	if(res != LSM6DS3_SUCCESS) 
	{
		return res;
	}

	res = LSM6DS3_gyro_SetFullScale(client,LSM6DS3_GYRO_RANGE_2000DPS);//we have only this choice
	if(res != LSM6DS3_SUCCESS) 
	{
		return res;
	}

	// 
	res = LSM6DS3_gyro_SetSampleRate(client, LSM6DS3_GYRO_ODR_104HZ);
	if(res != LSM6DS3_SUCCESS ) 
	{
		return res;
	}
	res = LSM6DS3_gyro_SetPowerMode(client, enable);
	if(res != LSM6DS3_SUCCESS)
	{
		return res;
	}

	GYRO_LOG("LSM6DS3_gyro_init_client OK!\n");
	//acc setting
	
	
#ifdef CONFIG_LSM6DS3_LOWPASS
	memset(&obj->fir, 0x00, sizeof(obj->fir));  
#endif

	return LSM6DS3_SUCCESS;
}
/*----------------------------------------------------------------------------*/
#ifdef LSM6DS3_GYRO_NEW_ARCH
static int lsm6ds3_gyro_open_report_data(int open)
{
    //should queuq work to report event if  is_report_input_direct=true
    return 0;
}

// if use  this typ of enable , Gsensor only enabled but not report inputEvent to HAL
static int lsm6ds3_gyro_enable_nodata(int en)
{
	int value = en;
	int err = 0;
	int cali[6];
	struct lsm6ds3_gyro_i2c_data *priv = obj_i2c_data;

	if(priv == NULL)
	{
		GYRO_ERR("obj_i2c_data is NULL!\n");
		return -1;
	}

	if(value == 1)
	{
		enable_status = true;
	}
	else
	{
		enable_status = false;
		// save dynamic cal
		cali[0] = (int)priv->cali_sw[0];
		cali[1] = (int)priv->cali_sw[1];
		cali[2] = (int)priv->cali_sw[2];
		cali[3] = 0;
		cali[4] = 0;
		cali[5] = 0;

		LSM6DS3_gyro_WriteCalibration(cali, "/persist-lg/sensor/gyro_dynamic_cal_data.txt");
	}
    
    if(atomic_read(&priv->trace) & GYRO_TRC_INFO)
    {
#ifdef LSM6DS3_VIRTUAL_SENSORS	    
        GYRO_LOG("gyroscope gy = %d, rv = %d, sensor_power =%d\n", enable_status, enable_status_rv, sensor_power);
#else
        GYRO_LOG("gyroscope gy = %d, sensor_power =%d\n", enable_status, sensor_power);
#endif
    }
    
	if(((value == 0) && (sensor_power == false)) ||((value == 1) && (sensor_power == true)))
	{
		GYRO_LOG("gyroscope device have updated!\n");
	}
	else
	{	
#ifdef LSM6DS3_VIRTUAL_SENSORS	
		err = LSM6DS3_gyro_SetPowerMode( priv->client, (enable_status || enable_status_rv));
#else
        err = LSM6DS3_gyro_SetPowerMode( priv->client, enable_status);
#endif

	}

    GYRO_LOG("lsm6ds3_gyro_enable_nodata OK!\n");
    return err;
}

static int lsm6ds3_gyro_set_delay(u64 ns)
{
    int value =0;
	int err =0;
    value = (int)ns/1000/1000;
	int sample_delay;
	struct lsm6ds3_gyro_i2c_data *priv = obj_i2c_data;

	if(priv == NULL)
	{
		GYRO_ERR("obj_i2c_data is NULL!\n");
		return -1;
	}

	if(value <= 5)
	{
		sample_delay = LSM6DS3_GYRO_ODR_208HZ;
	}
	else if(value <= 10)
	{
		sample_delay = LSM6DS3_GYRO_ODR_104HZ;
	}
	else
	{
		sample_delay = LSM6DS3_GYRO_ODR_52HZ;
	}
	err = LSM6DS3_gyro_SetSampleRate(priv->client, sample_delay);
	if(err != LSM6DS3_SUCCESS )
	{
		GYRO_ERR("Set delay parameter error!\n");
	}

	if(value >= 50)
	{
		atomic_set(&priv->filter, 0);
	}
	else
	{
		priv->fir.num = 0;
		priv->fir.idx = 0;
		priv->fir.sum[LSM6DS3_AXIS_X] = 0;
		priv->fir.sum[LSM6DS3_AXIS_Y] = 0;
		priv->fir.sum[LSM6DS3_AXIS_Z] = 0;
		atomic_set(&priv->filter, 1);
	}

    GYRO_LOG("mc3xxx_set_delay (%d), chip only use 1024HZ \n",value);
    return 0;
}

static int lsm6ds3_gyro_get_data(int* x ,int* y,int* z, int* status)
{
    char buff[LSM6DS3_BUFSIZE];
	struct lsm6ds3_gyro_i2c_data *priv = obj_i2c_data;
		
	if(priv == NULL)
	{
		GYRO_ERR("obj_i2c_data is NULL!\n");
		return -1;
	}
	if(atomic_read(&priv->trace) & GYRO_TRC_DATA)
	{
		GYRO_LOG("%s (%d),	\n",__FUNCTION__,__LINE__);
	}
	memset(buff, 0, sizeof(buff));
	LSM6DS3_ReadGyroData(priv->client, buff, LSM6DS3_BUFSIZE);
	
	sscanf(buff, "%x %x %x", x, y, z);				
	*status = SENSOR_STATUS_ACCURACY_HIGH;

    return 0;
}
#endif

#ifdef LSM6DS3_VIRTUAL_SENSORS
static int lsm6ds3_rotationvector_GetOpenStatus(void)
{
	if(enable_status_rv==false && status_change == true){
		status_change=false;
		return 0;
	}

	wait_event_interruptible(open_wq, (enable_status_rv != false));
	return enable_status_rv ? 1 : 0;
}

static int lsm6ds3_rotationvector_open_report_data(int open)
{
    //should queuq work to report event if  is_report_input_direct=true
    return 0;
}

static int lsm6ds3_rotationvector_enable_nodata(int en)
{
	int value = en;
	int err = 0;
	struct lsm6ds3_gyro_i2c_data *priv = obj_i2c_data;

	if(priv == NULL)
	{
		GYRO_ERR("obj_i2c_data is NULL!\n");
		return -1;
	}

	if(value == 1)
	{
		enable_status_rv = true;
        priv->quat[0] = 0;
        priv->quat[1] = 0;
        priv->quat[2] = 0;
        priv->quat[3] = CONVERT_RV_DIV;
	}
	else
	{
		enable_status_rv = false;
	}

	status_change =true;

    if(atomic_read(&priv->trace) & GYRO_TRC_INFO)
    {
        GYRO_LOG("rotationvector gy = %d, rv = %d, sensor_power =%d\n", enable_status, enable_status_rv, sensor_power);
    }
    
	if(((value == 0) && (sensor_power == false)) ||((value == 1) && (sensor_power == true)))
	{
		GYRO_LOG("rotationvector device have updated!\n");
	}
	else
	{
		err = LSM6DS3_gyro_SetPowerMode( priv->client, (enable_status_rv || enable_status));
	}
    
    wake_up(&open_wq);

    return err;
}

static int lsm6ds3_rotationvector_set_delay(u64 ns)
{
    int value =0;

    value = (int)ns/1000/1000;

	struct lsm6ds3_gyro_i2c_data *priv = obj_i2c_data;

	if(priv == NULL)
	{
		GYRO_ERR("obj_i2c_data is NULL!\n");
		return -1;
	}

    GYRO_LOG("lsm6ds3_gyro_set_delay (%d), chip only use 1024HZ \n",value);
    return 0;
}

int lsm6ds3_rotationvector_get_data(int *qx, int *qy, int *qz, int*qw, int *status)
{
	int err = 0;
	struct lsm6ds3_gyro_i2c_data *priv = obj_i2c_data;

	if(priv == NULL)
	{
		GYRO_ERR("obj_i2c_data is NULL!\n");
		return -1;
	}
	if(atomic_read(&priv->trace) & GYRO_TRC_DATA)
	{
		GYRO_LOG("%s (%d) RV=%d,%d,%d,%d,	\n",__FUNCTION__,__LINE__,
      priv->quat[0],priv->quat[1],priv->quat[2],priv->quat[3]);
	}

  *qx = priv->quat[0];
  *qy = priv->quat[1];
  *qz = priv->quat[2];
  *qw = priv->quat[3];

	return err;
}



#endif


#ifndef LSM6DS3_GYRO_NEW_ARCH
/*----------------------------------------------------------------------------*/
int LSM6DS3_gyro_operate(void* self, uint32_t command, void* buff_in, int size_in,
		void* buff_out, int size_out, int* actualout)
{
	int err = 0;
	int value;	
	struct lsm6ds3_gyro_i2c_data *priv = (struct lsm6ds3_gyro_i2c_data*)self;
	hwm_sensor_data* gyro_data;
	char buff[LSM6DS3_BUFSIZE];	
       printk("===>LSM6DS3_gyro_operate cmd=%d\n", command);
	switch (command)
	{
		case SENSOR_DELAY:
			if((buff_in == NULL) || (size_in < sizeof(int)))
			{
				GYRO_ERR("Set delay parameter error!\n");
				err = -EINVAL;
			}
			else
			{
			
			}
			break;

		case SENSOR_ENABLE:
			if((buff_in == NULL) || (size_in < sizeof(int)))
			{
				GYRO_ERR("Enable gyroscope parameter error!\n");
				err = -EINVAL;
			}
			else
			{
				value = *(int *)buff_in;
				if(value == 1)
				{
					enable_status = true;
				}
				else
				{
					enable_status = false;
				}
				GYRO_LOG("enable value=%d, sensor_power =%d\n",value,sensor_power);
				if(((value == 0) && (sensor_power == false)) ||((value == 1) && (sensor_power == true)))
				{
					GYRO_LOG("gyroscope device have updated!\n");
				}
				else
				{	
					err = LSM6DS3_gyro_SetPowerMode( priv->client, enable_status);	
				}

			}
			break;

		case SENSOR_GET_DATA:
			if((buff_out == NULL) || (size_out< sizeof(hwm_sensor_data)))
			{
				GYRO_ERR("get gyroscope data parameter error!\n");
				err = -EINVAL;
			}
			else
			{
				gyro_data = (hwm_sensor_data *)buff_out;
				LSM6DS3_ReadGyroData(priv->client, buff, LSM6DS3_BUFSIZE);
				sscanf(buff, "%x %x %x", &gyro_data->values[0], 
									&gyro_data->values[1], &gyro_data->values[2]);				
				gyro_data->status = SENSOR_STATUS_ACCURACY_HIGH;
				gyro_data->value_divide = 1000;
				if(atomic_read(&priv->trace) & GYRO_TRC_DATA)
				{
					GYRO_LOG("===>LSM6DS3_gyro_operate x=%d,y=%d,z=%d \n", gyro_data->values[0],gyro_data->values[1],gyro_data->values[2]);
				}
			}
			break;
		default:
			GYRO_ERR("gyroscope operate function no this parameter %d!\n", command);
			err = -1;
			break;
	}

	return err;
}
#endif
/****************************************************************************** 
 * Function Configuration
******************************************************************************/
static int lsm6ds3_open(struct inode *inode, struct file *file)
{
	file->private_data = lsm6ds3_i2c_client;

	if(file->private_data == NULL)
	{
		GYRO_ERR("null pointer!!\n");
		return -EINVAL;
	}
	return nonseekable_open(inode, file);
}
/*----------------------------------------------------------------------------*/
static int lsm6ds3_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}
/*----------------------------------------------------------------------------*/
#ifdef CONFIG_COMPAT
static long lsm6ds3_gyro_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret;

	void __user *arg32 = compat_ptr(arg);
	
	if (!file->f_op || !file->f_op->unlocked_ioctl)
		return -ENOTTY;
	
	switch (cmd) {
		 case COMPAT_GYROSCOPE_IOCTL_INIT:
			 if(arg32 == NULL)
			 {
				 GYRO_ERR("invalid argument.");
				 return -EINVAL;
			 }

			 ret = file->f_op->unlocked_ioctl(file, GYROSCOPE_IOCTL_INIT,
							(unsigned long)arg32);
			 if (ret){
			 	GYRO_ERR("GYROSCOPE_IOCTL_INIT unlocked_ioctl failed.");
				return ret;
			 }			 

			 break;

		 case COMPAT_GYROSCOPE_IOCTL_SET_CALI:
			 if(arg32 == NULL)
			 {
				 GYRO_ERR("invalid argument.");
				 return -EINVAL;
			 }

			 ret = file->f_op->unlocked_ioctl(file, GYROSCOPE_IOCTL_SET_CALI,
							(unsigned long)arg32);
			 if (ret){
			 	GYRO_ERR("GYROSCOPE_IOCTL_SET_CALI unlocked_ioctl failed.");
				return ret;
			 }			 

			 break;

		 case COMPAT_GYROSCOPE_IOCTL_CLR_CALI:
			 if(arg32 == NULL)
			 {
				 GYRO_ERR("invalid argument.");
				 return -EINVAL;
			 }

			 ret = file->f_op->unlocked_ioctl(file, GYROSCOPE_IOCTL_CLR_CALI,
							(unsigned long)arg32);
			 if (ret){
			 	GYRO_ERR("GYROSCOPE_IOCTL_CLR_CALI unlocked_ioctl failed.");
				return ret;
			 }			 

			 break;

		 case COMPAT_GYROSCOPE_IOCTL_GET_CALI:
			 if(arg32 == NULL)
			 {
				 GYRO_ERR("invalid argument.");
				 return -EINVAL;
			 }

			 ret = file->f_op->unlocked_ioctl(file, GYROSCOPE_IOCTL_GET_CALI,
							(unsigned long)arg32);
			 if (ret){
			 	GYRO_ERR("GYROSCOPE_IOCTL_GET_CALI unlocked_ioctl failed.");
				return ret;
			 }			 

			 break;

		 case COMPAT_GYROSCOPE_IOCTL_READ_SENSORDATA:
			 if(arg32 == NULL)
			 {
				 GYRO_ERR("invalid argument.");
				 return -EINVAL;
			 }

			 ret = file->f_op->unlocked_ioctl(file, GYROSCOPE_IOCTL_READ_SENSORDATA,
							(unsigned long)arg32);
			 if (ret){
			 	GYRO_ERR("GYROSCOPE_IOCTL_READ_SENSORDATA unlocked_ioctl failed.");
				return ret;
			 }			 

			 break;	
			 
		 default:
			 printk(KERN_ERR "%s not supported = 0x%04x", __FUNCTION__, cmd);
			 return -ENOIOCTLCMD;
			 break;
	}
	return ret;
}
#endif

static long lsm6ds3_gyro_unlocked_ioctl(struct file *file, unsigned int cmd,
       unsigned long arg)
{
	struct i2c_client *client = (struct i2c_client*)file->private_data;
	struct lsm6ds3_gyro_i2c_data *obj = i2c_get_clientdata(client);

	char strbuf[LSM6DS3_BUFSIZE] = {0};
	void __user *data;
	long err = 0;
	int status = 0;
	int copy_cnt = 0;
	struct SENSOR_DATA sensor_data;
	int cali[6] = {0};
	int smtRes=0;
	//GYRO_FUN();
	
	if(_IOC_DIR(cmd) & _IOC_READ)
	{
		err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
	}
	else if(_IOC_DIR(cmd) & _IOC_WRITE)
	{
		err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
	}

	if(err)
	{
		GYRO_ERR("access error: %08X, (%2d, %2d)\n", cmd, _IOC_DIR(cmd), _IOC_SIZE(cmd));
		return -EFAULT;
	}

	switch(cmd)
	{
		case GYROSCOPE_IOCTL_INIT:
			LSM6DS3_gyro_init_client(client, false);			
			break;

		case GYROSCOPE_IOCTL_SMT_DATA:
			data = (void __user *) arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;	  
			}

			GYRO_LOG("IOCTL smtRes: %d!\n", smtRes);
			copy_cnt = copy_to_user(data, &smtRes,  sizeof(smtRes));
			
			if(copy_cnt)
			{
				err = -EFAULT;
				GYRO_ERR("copy gyro data to user failed!\n");
			}	
			GYRO_LOG("copy gyro data to user OK: %d!\n", copy_cnt);
			break;
			

		case GYROSCOPE_IOCTL_READ_SENSORDATA:
			data = (void __user *) arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;	  
			}
			
			LSM6DS3_ReadGyroData(client, strbuf, LSM6DS3_BUFSIZE);
			if(copy_to_user(data, strbuf, sizeof(strbuf)))
			{
				err = -EFAULT;
				break;	  
			}				 
			break;

		case GYROSCOPE_IOCTL_SET_CALI:
			data = (void __user*)arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;	  
			}
			if(copy_from_user(&sensor_data, data, sizeof(sensor_data)))
			{
				err = -EFAULT;
				break;	  
			}
#ifdef CALIBRATION_TO_FILE
			if(make_cal_data_file() == LSM6DS3_SUCCESS)
			{
				err = LSM6DS3_gyro_ReadCalibration(cali, "/persist-lg/sensor/gyro_dynamic_cal_data.txt");
				if(err != 0 || (cali[0]==0 && cali[1]==0 && cali[2]==0) ){
					GYRO_ERR("dynamic cal file did not exist\n");
					err = LSM6DS3_gyro_ReadCalibration(cali, "/persist-lg/sensor/gyro_cal_data.txt");
					if(err !=0){
						GYRO_ERR("Read Cal Fail from file!!\n");
						break;
					}else{
						sensor_data.x = cali[0];
						sensor_data.y = cali[1];
						sensor_data.z = cali[2];
					}

				}else{
					sensor_data.x = cali[0];
					sensor_data.y = cali[1];
					sensor_data.z = cali[2];
				}
			}
#endif
			obj->cali_sw[LSM6DS3_AXIS_X] = cali[LSM6DS3_AXIS_X];
			obj->cali_sw[LSM6DS3_AXIS_Y] = cali[LSM6DS3_AXIS_Y];
			obj->cali_sw[LSM6DS3_AXIS_Z] = cali[LSM6DS3_AXIS_Z];
		break;

		case GYROSCOPE_IOCTL_CLR_CALI:
			err = LSM6DS3_gyro_ResetCalibration(client);
			break;

		case GYROSCOPE_IOCTL_GET_CALI:
			break;
#ifdef LSM6DS3_VIRTUAL_SENSORS
    case GYROSCOPE_IOC_SET_RVDATA:
			data = (void __user*)arg;
			if(data == NULL)
			{
				err = -EINVAL;
				break;
			}

			if(copy_from_user(obj->quat, data, sizeof(int)*4))
			{
				err = -EFAULT;
				break;
			}

      break;


    case GYROSCOPE_IOC_GET_OPEN_STATUS:
        status = lsm6ds3_rotationvector_GetOpenStatus();
			if(copy_to_user(arg, &status, sizeof(status)))
			{
				GYRO_LOG("copy_to_user failed.");
				return -EFAULT;
			}
            break;
#endif
		default:
			GYRO_ERR("unknown IOCTL: 0x%08x\n", cmd);
			err = -ENOIOCTLCMD;
			break;			
	}
	return err;
}

#if 1
/*----------------------------------------------------------------------------*/
static struct file_operations lsm6ds3_gyro_fops = {
	.owner = THIS_MODULE,
	.open = lsm6ds3_open,
	.release = lsm6ds3_release,
	.unlocked_ioctl = lsm6ds3_gyro_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = lsm6ds3_gyro_compat_ioctl,
#endif
};

/*----------------------------------------------------------------------------*/
static struct miscdevice lsm6ds3_gyro_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "gyroscope",
	.fops = &lsm6ds3_gyro_fops,
};
#endif

/*----------------------------------------------------------------------------*/
#ifndef CONFIG_HAS_EARLYSUSPEND
/*----------------------------------------------------------------------------*/
static int lsm6ds3_gyro_suspend(struct i2c_client *client, pm_message_t msg) 
{
	struct lsm6ds3_gyro_i2c_data *obj = i2c_get_clientdata(client);    
	int err = 0;
	
	GYRO_FUN(); 

	if(msg.event == PM_EVENT_SUSPEND)
	{   
		if(obj == NULL)
		{
			GYRO_ERR("null pointer!!\n");
			return -1;;
		}
		atomic_set(&obj->suspend, 1);
		err = LSM6DS3_gyro_SetPowerMode(obj->client, false);
		if(err)
		{
			GYRO_ERR("write power control fail!!\n");
			return err;
		}
		
		sensor_power = false;
		
		LSM6DS3_power(obj->hw, 0);

	}
	return err;
}
/*----------------------------------------------------------------------------*/
static int lsm6ds3_gyro_resume(struct i2c_client *client)
{
	struct lsm6ds3_gyro_i2c_data *obj = i2c_get_clientdata(client);
	int err;
	GYRO_FUN();

	if(obj == NULL)
	{
		GYRO_ERR("null pointer!!\n");
		return -EINVAL;
	}

#ifdef SUPPORT_DYNAMIC_UPDATE_GYRO_BIAS
	dynamic_gyro.dynamic_update_gyro_bias = 0;
	dynamic_gyro.stable_check_count = 0;
	dynamic_gyro.new_bias_x = 0;
	dynamic_gyro.new_bias_y = 0;
	dynamic_gyro.new_bias_z = 0;
	//GYRO_ERR("d_gyro Gyro resume, init bias %d %d %d\n", obj->cali_sw[0], obj->cali_sw[1], obj->cali_sw[2]);
#endif

	LSM6DS3_power(obj->hw, 1);
	atomic_set(&obj->suspend, 0);
#ifdef LSM6DS3_VIRTUAL_SENSORS
    err = LSM6DS3_gyro_SetPowerMode(obj->client, (enable_status_rv || enable_status));
#else
    err = LSM6DS3_gyro_SetPowerMode(obj->client, enable_status);
#endif

	if(err)
	{
		GYRO_ERR("initialize client fail! err code %d!\n", err);
		return err;        
	}

	return 0;
}
/*----------------------------------------------------------------------------*/
#else /*CONFIG_HAS_EARLY_SUSPEND is defined*/
/*----------------------------------------------------------------------------*/
static void lsm6ds3_gyro_early_suspend(struct early_suspend *h) 
{
	struct lsm6ds3_gyro_i2c_data *obj = container_of(h, struct lsm6ds3_gyro_i2c_data, early_drv);   
	int err;
	GYRO_FUN();    

	if(obj == NULL)
	{
		GYRO_ERR("null pointer!!\n");
		return;
	}
	atomic_set(&obj->suspend, 1);
	err = LSM6DS3_gyro_SetPowerMode(obj->client, false);
	if(err)
	{
		GYRO_ERR("write power control fail!!\n");
		return;
	}

	sensor_power = false;
	
	LSM6DS3_power(obj->hw, 0);
}
/*----------------------------------------------------------------------------*/
static void lsm6ds3_gyro_late_resume(struct early_suspend *h)
{
	struct lsm6ds3_gyro_i2c_data *obj = container_of(h, struct lsm6ds3_gyro_i2c_data, early_drv);         
	int err;
	GYRO_FUN();

	if(obj == NULL)
	{
		GYRO_ERR("null pointer!!\n");
		return;
	}

	LSM6DS3_power(obj->hw, 1);
#ifdef LSM6DS3_VIRTUAL_SENSORS
    err = LSM6DS3_gyro_SetPowerMode(obj->client, (enable_status_rv || enable_status));
#else
    err = LSM6DS3_gyro_SetPowerMode(obj->client, enable_status);
#endif
	if(err)
	{
		GYRO_ERR("initialize client fail! err code %d!\n", err);
		return;        
	}
	atomic_set(&obj->suspend, 0);    
}
/*----------------------------------------------------------------------------*/
#endif /*CONFIG_HAS_EARLYSUSPEND*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
static int lsm6ds3_gyro_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct i2c_client *new_client;
	struct lsm6ds3_gyro_i2c_data *obj;

#ifdef LSM6DS3_GYRO_NEW_ARCH
	struct gyro_control_path ctl={0};
    struct gyro_data_path data={0};
#else
	struct hwmsen_object gyro_sobj;
#endif
	int err = 0;
	GYRO_FUN();
    
	if(!(obj = kzalloc(sizeof(*obj), GFP_KERNEL)))
	{
		err = -ENOMEM;
		goto exit;
	}
	
	memset(obj, 0, sizeof(struct lsm6ds3_gyro_i2c_data));

	obj->hw = gy_hw;
		
	err = hwmsen_get_convert(obj->hw->direction, &obj->cvt);
	if(err)
	{
		GYRO_ERR("invalid direction: %d\n", obj->hw->direction);
		goto exit;
	}

	client->addr = 0x6B;
	obj_i2c_data = obj;
	obj->client = client;
	new_client = obj->client;
	i2c_set_clientdata(new_client,obj);
	
	atomic_set(&obj->trace, 0);
	atomic_set(&obj->suspend, 0);
	
	lsm6ds3_i2c_client = new_client;	
	err = LSM6DS3_gyro_init_client(new_client, false);
	if(err)
	{
		goto exit_init_failed;
	}
	
#if 1
	err = misc_register(&lsm6ds3_gyro_device);
	if(err)
	{
		GYRO_ERR("lsm6ds3_gyro_device misc register failed!\n");
		goto exit_misc_device_register_failed;
	}
#endif

#ifdef LSM6DS3_GYRO_NEW_ARCH
	err = lsm6ds3_create_attr(&(lsm6ds3_gyro_init_info.platform_diver_addr->driver));
#else
	err = lsm6ds3_create_attr(&lsm6ds3_driver.driver);
#endif
	if(err)
	{
		GYRO_ERR("lsm6ds3 create attribute err = %d\n", err);
		goto exit_create_attr_failed;
	}	
#ifndef LSM6DS3_GYRO_NEW_ARCH

	gyro_sobj.self = obj;
    gyro_sobj.polling = 1;
    gyro_sobj.sensor_operate = LSM6DS3_gyro_operate;
	err = hwmsen_attach(ID_GYROSCOPE, &gyro_sobj);
	if(err)
	{
		GYRO_ERR("hwmsen_attach Gyroscope fail = %d\n", err);
		goto exit_kfree;
	}
#else
    ctl.open_report_data= lsm6ds3_gyro_open_report_data;
    ctl.enable_nodata = lsm6ds3_gyro_enable_nodata;
    ctl.set_delay  = lsm6ds3_gyro_set_delay;
    ctl.is_report_input_direct = false;
    ctl.is_support_batch = obj->hw->is_batch_supported;

    err = gyro_register_control_path(&ctl);
    if(err)
    {
         GYRO_ERR("register gyoscope control path err\n");
        goto exit_kfree;
    }

    data.get_data = lsm6ds3_gyro_get_data;
    data.vender_div = DEGREE_TO_RAD;
    err = gyro_register_data_path(&data);
    if(err)
    {
        GYRO_ERR("register gyroscope data path err= %d\n", err);
        goto exit_kfree;
    }
#endif

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

#ifdef CONFIG_HAS_EARLYSUSPEND
	obj->early_drv.level    = EARLY_SUSPEND_LEVEL_DISABLE_FB - 1,
	obj->early_drv.suspend  = lsm6ds3_gyro_early_suspend,
	obj->early_drv.resume   = lsm6ds3_gyro_late_resume,    
	register_early_suspend(&obj->early_drv);
#endif 
#ifdef LSM6DS3_GYRO_NEW_ARCH
	lsm6ds3_gyro_init_flag = 0;
#endif
	GYRO_LOG("%s: OK\n", __func__);    
	return 0;

	exit_create_attr_failed:
	misc_deregister(&lsm6ds3_gyro_device);
	exit_misc_device_register_failed:
	exit_init_failed:
	//i2c_detach_client(new_client);
	exit_kfree:
	kfree(obj);
	exit:
#ifdef LSM6DS3_GYRO_NEW_ARCH
	lsm6ds3_gyro_init_flag = -1;
#endif
	GYRO_ERR("%s: err = %d\n", __func__, err);        
	return err;
}

/*----------------------------------------------------------------------------*/
static int lsm6ds3_gyro_i2c_remove(struct i2c_client *client)
{
	int err = 0;	
#ifndef LSM6DS3_GYRO_NEW_ARCH

	err = lsm6ds3_delete_attr(&lsm6ds3_driver.driver);
#else
	err = lsm6ds3_delete_attr(&(lsm6ds3_gyro_init_info.platform_diver_addr->driver));
#endif
	if(err)
	{
		GYRO_ERR("lsm6ds3_gyro_i2c_remove fail: %d\n", err);
	}

	#if 1
	err = misc_deregister(&lsm6ds3_gyro_device);
	if(err)
	{
		GYRO_ERR("misc_deregister lsm6ds3_gyro_device fail: %d\n", err);
	}
	#endif

	lsm6ds3_i2c_client = NULL;
	i2c_unregister_device(client);
	kfree(i2c_get_clientdata(client));
	return 0;
}

#ifdef LSM6DS3_VIRTUAL_SENSORS
static int lsm6ds3_rotationvector_local_init(void){
//    struct rotationvector_drv_obj rv_sobj;
    struct rotationvector_control_path rv_ctl = {0};
    struct rotationvector_data_path rv_data = {0};
	int err;

	if(lsm6ds3_gyro_init_flag == -1)
	{
		GYRO_ERR("%s init failed!\n", __FUNCTION__);
		return -1;
	}
    enable_status_rv = false;
    init_waitqueue_head(&open_wq);

    rv_ctl.enable_nodata = lsm6ds3_rotationvector_enable_nodata;
    rv_ctl.is_report_input_direct = false;
    rv_ctl.is_support_batch = false;
    rv_ctl.open_report_data = lsm6ds3_rotationvector_open_report_data;
    rv_ctl.set_delay = lsm6ds3_rotationvector_set_delay;
    err = rotationvector_register_control_path(&rv_ctl);
    if (err)
    {
         GYRO_ERR("register rotationvector control path err\n");
        return err;
    }

    rv_data.get_data = lsm6ds3_rotationvector_get_data;
    rv_data.get_raw_data = NULL;
    rv_data.vender_div = CONVERT_RV_DIV;
    err = rotationvector_register_data_path(&rv_data);
    if (err)
    {
        GYRO_ERR("register rotationvector data path err= %d\n", err);
        return err;
    }

	return 0;
}

static int lsm6ds3_rotationvector_local_uninit(void){
	return 0;
}
#endif


/*----------------------------------------------------------------------------*/
#ifndef LSM6DS3_GYRO_NEW_ARCH
static int lsm6ds3_gyro_probe(struct platform_device *pdev) 
{
	GYRO_FUN();

	LSM6DS3_power(gy_hw, 1);
	
	if(i2c_add_driver(&lsm6ds3_gyro_i2c_driver))
	{
		GYRO_ERR("add driver error\n");
		return -1;
	}
	return 0;
}
/*----------------------------------------------------------------------------*/
static int lsm6ds3_gyro_remove(struct platform_device *pdev)
{
	
    //GYRO_FUN();    
    LSM6DS3_power(gy_hw, 0);  	
    i2c_del_driver(&lsm6ds3_gyro_i2c_driver);
    return 0;
}

/*----------------------------------------------------------------------------*/
#ifdef CONFIG_OF
static const struct of_device_id gyroscope_of_match[] = {
	{ .compatible = "mediatek,gyroscope", },
	{},
};
#endif

static struct platform_driver lsm6ds3_driver = {
	.probe      = lsm6ds3_gyro_probe,
	.remove     = lsm6ds3_gyro_remove,    
	.driver     = {
			.name  = "gyroscope",
		//	.owner	= THIS_MODULE,
	#ifdef CONFIG_OF
			.of_match_table = gyroscope_of_match,
	#endif

	}
};
#else
static int lsm6ds3_gyro_local_init(void)
{
	GYRO_FUN();

	LSM6DS3_power(gy_hw, 1);

	if(i2c_add_driver(&lsm6ds3_gyro_i2c_driver))
	{
		GYRO_ERR("add driver error\n");
		return -1;
	}
	if(lsm6ds3_gyro_init_flag == -1)
	{
		GYRO_ERR("%s init failed!\n", __FUNCTION__);
		return -1;
	}
	return 0;
}
static int lsm6ds3_gyro_local_uninit(void)
{

    GYRO_FUN();
    LSM6DS3_power(gy_hw, 0);
    i2c_del_driver(&lsm6ds3_gyro_i2c_driver);
    return 0;
}
#endif
/*----------------------------------------------------------------------------*/
static int __init lsm6ds3_gyro_init(void)
{
	//GYRO_FUN();
	const char *name ="mediatek,lsm6ds3_gyro";

	gy_hw = get_gyro_dts_func(name,gy_hw);
	if(!gy_hw)
		GYRO_LOG("get dts info fail\n");
#ifdef LSM6DS3_VIRTUAL_SENSORS
	rotationvector_driver_add(&lsm6ds3_rotationvector_info);
#endif
#ifndef LSM6DS3_GYRO_NEW_ARCH
	if(platform_driver_register(&lsm6ds3_driver))
	{
		GYRO_ERR("failed to register driver");
		return -ENODEV;
	}
#else
    gyro_driver_add(&lsm6ds3_gyro_init_info);
#endif
	return 0;    
}
/*----------------------------------------------------------------------------*/
static void __exit lsm6ds3_gyro_exit(void)
{
	GYRO_FUN();
#ifndef LSM6DS3_GYRO_NEW_ARCH
	platform_driver_unregister(&lsm6ds3_driver);
#endif
}
/*----------------------------------------------------------------------------*/
module_init(lsm6ds3_gyro_init);
module_exit(lsm6ds3_gyro_exit);
/*----------------------------------------------------------------------------*/
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LSM6DS3 Accelerometer and gyroscope driver");
MODULE_AUTHOR("xj.wang@mediatek.com, darren.han@st.com");






/*----------------------------------------------------------------- LSM6DS3 ------------------------------------------------------------------*/
