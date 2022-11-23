/* lsm6ds3.h
  *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef L3M6DS3_GY_H
#define L3M6DS3_GY_H
	 
#include <linux/ioctl.h>
	 
#define LSM6DS3_I2C_SLAVE_ADDR		0xD0
#define LSM6DS3_FIXED_DEVID			0x69


/* LSM6DS3 Register Map  (Please refer to LSM6DS3 Specifications) */
#define LSM6DS3_FUNC_CFG_ACCESS  0x00
#define LSM6DS3_SENSOR_SYNC_TIME_FRAME 0X04

/*FIFO control register*/
#define LSM6DS3_FIFO_CTRL1 0x06
#define LSM6DS3_FIFO_CTRL2 0x07
#define LSM6DS3_FIFO_CTRL3 0x08
#define LSM6DS3_FIFO_CTRL4 0x09
#define LSM6DS3_FIFO_CTRL5 0x0a

/*Orientation configuration*/
#define LSM6DS3_ORIENT_CFG_G 0x0B

/*Interrupt control*/
#define LSM6DS3_INT1_CTRL 0x0D
#define LSM6DS3_INT2_CTRL 0x0E

#define LSM6DS3_WHO_AM_I 0x0F

/*Acc and Gyro control registers*/
#define LSM6DS3_CTRL1_XL 0x10
#define LSM6DS3_CTRL2_G 0x11
#define LSM6DS3_CTRL3_C	0x12
#define LSM6DS3_CTRL4_C	0x13
#define LSM6DS3_CTRL5_C	0x14
#define LSM6DS3_CTRL6_C	0x15
#define LSM6DS3_CTRL7_G	0x16
#define LSM6DS3_CTRL8_XL	0x17
#define LSM6DS3_CTRL9_XL	0x18
#define LSM6DS3_CTRL10_C	0x19

#define LSM6DS3_MASTER_CONFIG 0x1A
#define LSM6DS3_WAKE_UP_SRC 0x1B
#define LSM6DS3_TAP_SRC 0x1C
#define LSM6DS3_D6D_SRC	0x1D
#define LSM6DS3_STATUS_REG 0x1E

/*Output register*/
#define LSM6DS3_OUT_TEMP_L 0x20
#define LSM6DS3_OUT_TEMP_H 0x21
#define LSM6DS3_OUTX_L_G   0x22
#define LSM6DS3_OUTX_H_G   0x23
#define LSM6DS3_OUTY_L_G   0x24
#define LSM6DS3_OUTY_H_G   0x25
#define LSM6DS3_OUTZ_L_G   0x26
#define LSM6DS3_OUTZ_H_G   0x27

#define LSM6DS3_OUTX_L_XL   0x28
#define LSM6DS3_OUTX_H_XL   0x29
#define LSM6DS3_OUTY_L_XL   0x2A
#define LSM6DS3_OUTY_H_XL   0x2B
#define LSM6DS3_OUTZ_L_XL   0x2C
#define LSM6DS3_OUTZ_H_XL   0x2D

/*Sensor Hub registers*/
#define LSM6DS3_SENSORHUB1_REG 0x2E
#define LSM6DS3_SENSORHUB2_REG 0x2F
#define LSM6DS3_SENSORHUB3_REG 0x30
#define LSM6DS3_SENSORHUB4_REG 0x31
#define LSM6DS3_SENSORHUB5_REG 0x32
#define LSM6DS3_SENSORHUB6_REG 0x33
#define LSM6DS3_SENSORHUB7_REG 0x34
#define LSM6DS3_SENSORHUB8_REG 0x35
#define LSM6DS3_SENSORHUB9_REG 0x36
#define LSM6DS3_SENSORHUB10_REG 0x37
#define LSM6DS3_SENSORHUB11_REG 0x38
#define LSM6DS3_SENSORHUB12_REG 0x39

#define LSM6DS3_SENSORHUB13_REG 0x4D
#define LSM6DS3_SENSORHUB14_REG 0x4E
#define LSM6DS3_SENSORHUB15_REG 0x4F
#define LSM6DS3_SENSORHUB16_REG 0x50
#define LSM6DS3_SENSORHUB17_REG 0x51
#define LSM6DS3_SENSORHUB18_REG 0x52

/*FIFO status registers*/
#define LSM6DS3_FIFO_STATUS1 0x3A
#define LSM6DS3_FIFO_STATUS2 0x3B
#define LSM6DS3_FIFO_STATUS3 0x3C
#define LSM6DS3_FIFO_STATUS4 0x3D

#define LSM6DS3_FIFO_DATA_OUT_L 0x3E
#define LSM6DS3_FIFO_DATA_OUT_H 0x3F

#define LSM6DS3_TIMESTAMP0_REG 0x40
#define LSM6DS3_TIMESTAMP1_REG 0x41
#define LSM6DS3_TIMESTAMP2_REG 0x42

#define LSM6DS3_STEP_TIMESTAMP_L 0x49
#define LSM6DS3_STEP_TIMESTAMP_H 0x4A

#define LSM6DS3_STEP_COUNTER_L	0x4B
#define LSM6DS3_STEP_COUNTER_H	0x4C

#define LSM6DS3_FUNC_SRC 	0x53
#define LSM6DS3_TAP_CFG	0x58
#define LSM6DS3_TAP_THS_6D 0x59
#define LSM6DS3_INT_DUR2	0x5A
#define LSM6DS3_WAKE_UP_THS	0x5B
#define LSM6DS3_WAKE_UP_DUR 0x5C

#define LSM6DS3_FREE_FALL 0x5D
#define LSM6DS3_MD1_CFG 0x5E
#define LSM6DS3_MD2_CFG 0x5F

/*Output Raw data*/
#define LSM6DS3_OUT_MAG_RAW_X_L 0x66
#define LSM6DS3_OUT_MAG_RAW_X_H 0x67
#define LSM6DS3_OUT_MAG_RAW_Y_L 0x68
#define LSM6DS3_OUT_MAG_RAW_Y_H 0x69
#define LSM6DS3_OUT_MAG_RAW_Z_L 0x6A
#define LSM6DS3_OUT_MAG_RAW_Z_H 0x6B

/*Embedded function register*/
#define LSM6DS3_SLV0_ADD 0x02
#define LSM6DS3_SLV0_SUBADD 0x03
#define LSM6DS3_SLAVE0_CONFIG 0x04

#define LSM6DS3_SLV1_ADD 0x05
#define LSM6DS3_SLV1_SUBADD 0x06
#define LSM6DS3_SLAVE1_CONFIG 0x07

#define LSM6DS3_SLV2_ADD 0x08
#define LSM6DS3_SLV2_SUBADD 0x09
#define LSM6DS3_SLAVE2_CONFIG 0x0a

#define LSM6DS3_SLV3_ADD 0x0b
#define LSM6DS3_SLV3_SUBADD 0x0c
#define LSM6DS3_SLAVE3_CONFIG 0x0d

#define LSM6DS3_DATAWRITE_SRC_MODE_SUB_SLV0 0x0e
#define LSM6DS3_SM_THS 0x13
#define LSM6DS3_STEP_COUNT_DELTA 0x15
#define LSM6DS3_MAG_SI_XX 0x24
#define LSM6DS3_MAG_SI_XY 0x25
#define LSM6DS3_MAG_SI_XZ 0x26
#define LSM6DS3_MAG_SI_YX 0x27
#define LSM6DS3_MAG_SI_YY 0x28
#define LSM6DS3_MAG_SI_YZ 0x29
#define LSM6DS3_MAG_SI_ZX 0x2a
#define LSM6DS3_MAG_SI_ZY 0x2b
#define LSM6DS3_MAG_SI_ZZ 0x2c

#define LSM6DS3_MAG_OFFX_L 0x2D
#define LSM6DS3_MAG_OFFX_H 0x2E
#define LSM6DS3_MAG_OFFY_L 0x2F
#define LSM6DS3_MAG_OFFY_H 0x30
#define LSM6DS3_MAG_OFFZ_L 0x31
#define LSM6DS3_MAG_OFFZ_H 0x32




/*LSM6DS3 Register Bit definitions*/
#define LSM6DS3_ACC_RANGE_MASK 0x0C
#define LSM6DS3_ACC_RANGE_2g		    0x00
#define LSM6DS3_ACC_RANGE_4g		    0x08
#define LSM6DS3_ACC_RANGE_8g		    0x0C
#define LSM6DS3_ACC_RANGE_16g		    0x04

#define LSM6DS3_ACC_ODR_MASK	0xF0
#define LSM6DS3_ACC_ODR_POWER_DOWN		0x00
#define LSM6DS3_ACC_ODR_13HZ		    0x10
#define LSM6DS3_ACC_ODR_26HZ		    0x20
#define LSM6DS3_ACC_ODR_52HZ		    0x30
#define LSM6DS3_ACC_ODR_104HZ		    0x40
#define LSM6DS3_ACC_ODR_208HZ		    0x50
#define LSM6DS3_ACC_ODR_416HZ		    0x60
#define LSM6DS3_ACC_ODR_833HZ		    0x70
#define LSM6DS3_ACC_ODR_1660HZ		    0x80
#define LSM6DS3_ACC_ODR_3330HZ		    0x90
#define LSM6DS3_ACC_ODR_6660HZ		    0xA0

#define LSM6DS3_GYRO_RANGE_MASK	0x0E
#define LSM6DS3_GYRO_RANGE_125DPS	0x02
#define LSM6DS3_GYRO_RANGE_245DPS	0x00
#define LSM6DS3_GYRO_RANGE_500DPS	0x04
#define LSM6DS3_GYRO_RANGE_1000DPS	0x08
#define LSM6DS3_GYRO_RANGE_2000DPS	0x0c

#define LSM6DS3_GYRO_ODR_MASK	0xf0
#define LSM6DS3_GYRO_ODR_POWER_DOWN	0x00
#define LSM6DS3_GYRO_ODR_13HZ		0x10
#define LSM6DS3_GYRO_ODR_26HZ		0x20
#define LSM6DS3_GYRO_ODR_52HZ		0x30
#define LSM6DS3_GYRO_ODR_104HZ		0x40
#define LSM6DS3_GYRO_ODR_208HZ		0x50
#define LSM6DS3_GYRO_ODR_416HZ		0x60
#define LSM6DS3_GYRO_ODR_833HZ		0x70
#define LSM6DS3_GYRO_ODR_1660HZ		0x80

#define AUTO_INCREMENT 0x80
#define LSM6DS3_CTRL3_C_IFINC 0x04
#define LSM6DS3_CTRL3_C_BDU 0x40


#define LSM6DS3_ACC_SENSITIVITY_2G		61	/*ug/LSB*/
#define LSM6DS3_ACC_SENSITIVITY_4G		122	/*ug/LSB*/
#define LSM6DS3_ACC_SENSITIVITY_8G		244	/*ug/LSB*/
#define LSM6DS3_ACC_SENSITIVITY_16G		488	/*ug/LSB*/


#define LSM6DS3_ACC_ENABLE_AXIS_MASK 0X38
#define LSM6DS3_ACC_ENABLE_AXIS_X 0x08
#define LSM6DS3_ACC_ENABLE_AXIS_Y 0x10
#define LSM6DS3_ACC_ENABLE_AXIS_Z 0x20



#define LSM6DS3_GYRO_SENSITIVITY_125DPS		4375	/*udps/LSB*/
#define LSM6DS3_GYRO_SENSITIVITY_245DPS		8750	/*udps/LSB*/
#define LSM6DS3_GYRO_SENSITIVITY_500DPS		17500	/*udps/LSB*/
#define LSM6DS3_GYRO_SENSITIVITY_1000DPS	35000	/*udps/LSB*/
#define LSM6DS3_GYRO_SENSITIVITY_2000DPS	70000	/*udps/LSB*/

#define LSM6DS3_GYRO_ENABLE_AXIS_MASK 0x38
#define LSM6DS3_GYRO_ENABLE_AXIS_X 0x08
#define LSM6DS3_GYRO_ENABLE_AXIS_Y 0x10
#define LSM6DS3_GYRO_ENABLE_AXIS_Z 0x20

/* FIFO_CTRL5 */
#define LSM6DS3_FIFO_CTRL5_ODR_MSK          0x78
#define LSM6DS3_FIFO_CTRL5_ODR_13HZ         (0x01 << 3)
#define LSM6DS3_FIFO_CTRL5_ODR_26HZ         (0x02 << 3)
#define LSM6DS3_FIFO_CTRL5_ODR_52HZ         (0x03 << 3)
#define LSM6DS3_FIFO_CTRL5_ODR_104HZ        (0x04 << 3)
#define LSM6DS3_FIFO_CTRL5_ODR_208HZ        (0x05 << 3)
#define LSM6DS3_FIFO_CTRL5_ODR_416HZ        (0x06 << 3)
#define LSM6DS3_FIFO_CTRL5_ODR_833HZ        (0x07 << 3)
#define LSM6DS3_FIFO_CTRL5_ODR_1660HZ       (0x08 << 3)
#define LSM6DS3_FIFO_CTRL5_ODR_3330HZ       (0x09 << 3)
#define LSM6DS3_FIFO_CTRL5_ODR_6660HZ       (0x0A << 3)
#define LSM6DS3_FIFO_CTRL5_MODE_MSK         0x07
#define LSM6DS3_FIFO_CTRL5_MODE_BYPASS      0x00
#define LSM6DS3_FIFO_CTRL5_MODE_FIFO        0x01
#define LSM6DS3_FIFO_CTRL5_MODE_CONT        0x03
#define LSM6DS3_FIFO_CTRL5_MODE_BY2CONT     0x04
#define LSM6DS3_FIFO_CTRL5_MODE_CONT_OVR    0x06


#define LSM6DS3_SUCCESS		       0
#define LSM6DS3_ERR_I2C		      -1
#define LSM6DS3_ERR_STATUS			  -3
#define LSM6DS3_ERR_SETUP_FAILURE	  -4
#define LSM6DS3_ERR_GETGSENSORDATA  -5
#define LSM6DS3_ERR_IDENTIFICATION	  -6
#define LSM6DS3_ERR_SETUP_FAILURE     -7

#define LSM6DS3_BUFSIZE 60

#define LSM6DS3_DA_RETRY_COUNT        10
#define LSM6DS3_MIN_ST_2000DPS        (2857)          // 200dps @ FS2000dps
#define LSM6DS3_MAX_ST_2000DPS        (10000)         // 700dps @ FS2000dps
/* 40dps @ 2000dps ~ 572*/
#define LSM6DS3_MIN_ZERO_RATE         (-572)
#define LSM6DS3_MAX_ZERO_RATE         (572)
#define LSM6DS3_ZERO_RATE_DELTA       72              // 5dps @ FS2000dps

#define LSM6DS3_FIFO_DEPTH            128
#define LSM6DS3_FLAG_SLEEP            0x80

#define LSM6DS3_STATUS_REG_EV_BOOT          0x08
#define LSM6DS3_STATUS_REG_TDA              0x04
#define LSM6DS3_STATUS_REG_GDA              0x02
#define LSM6DS3_STATUS_REG_XLDA             0x01


#define LSM6DS3_ST_G_MASK       ( 0x03 << 2 )
#define LSM6DS3_ST_G_OFF        ( 0x00 << 2 )
#define LSM6DS3_ST_G_ON_POS     ( 0x01 << 2 )
#define LSM6DS3_ST_G_ON_NEG     ( 0x03 << 2 )


// 1 rad = 180/PI degree, L3G4200D_OUT_MAGNIFY = 131,
// 180*131/PI = 7506
#define DEGREE_TO_RAD	7506  //180*1000000/PI//7506  // fenggy mask
//#define DEGREE_TO_RAD 819	 


#define LSM6DS3_CALI_DATA_NUM         20
#define LSM6DS3_SHAKING_DETECT_THRESHOLD  714//50dps-> 50/0.07 = 714 LSB
#define LSM6DS3_CALIB_THRESH_LSB            (512) //40dps->40/0.07 = 512 LSB


#endif //L3M6DS3_GY_H

