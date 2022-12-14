/* touch_synaptics.c
 *
 * Copyright (C) 2015 LGE.
 *
 * Author: hoyeon.jang@lge.com
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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

/*
 *  Include to touch core Header File
 */
#include <touch_hwif.h>
#include <touch_core.h>

/*
 *  Include to Local Header File
 */
#include "touch_td4100.h"
#include "touch_td4100_prd.h"

/*
 * PLG591 - PH2
 */

const char *f_str[] = {
	"ERROR",
	"DISTANCE_INTER_TAP",
	"DISTANCE_TOUCHSLOP",
	"TIMEOUT_INTER_TAP",
	"MULTI_FINGER",
	"DELAY_TIME"
};

static struct synaptics_rmidev_exp_fhandler rmidev_fhandler;

bool synaptics_is_product(struct synaptics_data *d, const char *product_id, size_t len)
{
	return strncmp(d->ic_info.product_id, product_id, len)
	    ? false : true;
}

bool synaptics_is_img_product(struct synaptics_data *d, const char *product_id, size_t len)
{
	return strncmp(d->ic_info.img_product_id, product_id, len)
	    ? false : true;
}

int synaptics_read(struct device *dev, u8 addr, void *data, int size)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct touch_bus_msg msg;
	int ret = 0;

	ts->tx_buf[0] = addr;

	msg.tx_buf = ts->tx_buf;
	msg.tx_size = 1;

	msg.rx_buf = ts->rx_buf;
	msg.rx_size = size;

	ret = touch_bus_read(dev, &msg);

	if (ret < 0) {
		TOUCH_E("touch bus read error : %d\n", ret);
		return ret;
	}

	memcpy(data, &ts->rx_buf[0], size);
	return 0;
}

int synaptics_write(struct device *dev, u8 addr, void *data, int size)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct touch_bus_msg msg;
	int ret = 0;

	ts->tx_buf[0] = addr;
	memcpy(&ts->tx_buf[1], data, size);

	msg.tx_buf = ts->tx_buf;
	msg.tx_size = size + 1;
	msg.rx_buf = NULL;
	msg.rx_size = 0;

	ret = touch_bus_write(dev, &msg);

	if (ret < 0) {
		TOUCH_E("touch bus write error : %d\n", ret);
		return ret;
	}

	return 0;
}

static void synaptics_reset_ctrl(struct device *dev, int ctrl)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);
	u8 wdata = 0x01;

	switch (ctrl) {
	default:
	case SW_RESET:
		TOUCH_I("%s : SW Reset\n", __func__);
		synaptics_write(dev, DEVICE_COMMAND_REG, &wdata, sizeof(u8));
		touch_msleep(ts->caps.sw_reset_delay);
		break;
	case HW_RESET:
		TOUCH_I("%s : HW Reset\n", __func__);
		touch_gpio_direction_output(ts->reset_pin, 0);
		touch_msleep(1);
		touch_gpio_direction_output(ts->reset_pin, 1);
		touch_msleep(ts->caps.hw_reset_delay);
		break;
	}
}

static int synaptics_abs_enable(struct device *dev, bool enable)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	u8 val;
	int ret;

	ret = synaptics_read(dev, INTERRUPT_ENABLE_REG, &val, sizeof(val));
	if (ret < 0) {
		TOUCH_E("failed to read interrupt enable - ret:%d\n", ret);
		return ret;
	}

	if (enable)
		val |= INTERRUPT_MASK_ABS0;
	else
		val &= ~INTERRUPT_MASK_ABS0;


	ret = synaptics_write(dev, INTERRUPT_ENABLE_REG, &val, sizeof(val));
	if (ret < 0) {
		TOUCH_E("failed to write interrupt enable - ret:%d\n", ret);
		return ret;
	}

	return 0;
}

int synaptics_set_page(struct device *dev, u8 page)
{
	int ret = synaptics_write(dev, PAGE_SELECT_REG, &page, 1);

	if (ret >= 0)
		to_synaptics_data(dev)->curr_page = page;

	return ret;
}

static int synaptics_get_f12(struct device *dev)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	u8 query_5_data[5];
	u8 query_8_data[3];
	u8 ctrl_23_data[2];
	u8 ctrl_8_data[14];

	u32 query_5_present = 0;
	u16 query_8_present = 0;

	u8 offset;
	int i;
	int ret;

	ret = synaptics_read(dev, d->f12.dsc.query_base + 5, query_5_data, sizeof(query_5_data));

	if (ret < 0) {
		TOUCH_E("faied to get query5 (ret: %d)\n", ret);
		return ret;
	}

	query_5_present = (query_5_present << 8) | query_5_data[4];
	query_5_present = (query_5_present << 8) | query_5_data[3];
	query_5_present = (query_5_present << 8) | query_5_data[2];
	query_5_present = (query_5_present << 8) | query_5_data[1];
	TOUCH_I("qeury_5_present=0x%08X [%02X %02X %02X %02X %02X]\n",
		query_5_present, query_5_data[0], query_5_data[1],
		query_5_data[2], query_5_data[3], query_5_data[4]);

	for (i = 0, offset = 0; i < 32; i++) {
		d->f12_reg.ctrl[i] = d->f12.dsc.control_base + offset;

		if (query_5_present & (1 << i)) {
			TOUCH_I("f12_reg.ctrl[%d]=0x%02X (0x%02x+%d)\n",
				i, d->f12_reg.ctrl[i], d->f12.dsc.control_base, offset);
			offset++;
		}
	}

	ret = synaptics_read(dev, d->f12.dsc.query_base + 8, query_8_data, sizeof(query_8_data));

	if (ret < 0) {
		TOUCH_E("faied to get query8 (ret: %d)\n", ret);
		return ret;
	}

	query_8_present = (query_8_present << 8) | query_8_data[2];
	query_8_present = (query_8_present << 8) | query_8_data[1];
	TOUCH_I("qeury_8_present=0x%08X [%02X %02X %02X]\n",
		query_8_present, query_8_data[0], query_8_data[1], query_8_data[2]);

	for (i = 0, offset = 0; i < 16; i++) {
		d->f12_reg.data[i] = d->f12.dsc.data_base + offset;

		if (query_8_present & (1 << i)) {
			TOUCH_I("d->f12_reg.data[%d]=0x%02X (0x%02x+%d)\n",
				i, d->f12_reg.data[i], d->f12.dsc.data_base, offset);
			offset++;
		}
	}

	ret = synaptics_read(dev, d->f12_reg.ctrl[23], ctrl_23_data, sizeof(ctrl_23_data));

	if (ret < 0) {
		TOUCH_E("faied to get f12_ctrl32_data (ret: %d)\n", ret);
		return ret;
	}

	d->object_report = ctrl_23_data[0];
	d->num_of_fingers = min_t(u8, ctrl_23_data[1], (u8) MAX_NUM_OF_FINGERS);

	TOUCH_I("object_report[0x%02X], num_of_fingers[%d]\n", d->object_report, d->num_of_fingers);

	ret = synaptics_read(dev, d->f12_reg.ctrl[8], ctrl_8_data, sizeof(ctrl_8_data));

	if (ret < 0) {
		TOUCH_E("faied to get f12_ctrl8_data (ret: %d)\n", ret);
		return ret;
	}

	TOUCH_I("ctrl_8-sensor_max_x[%d], sensor_max_y[%d]\n",
		((u16) ctrl_8_data[0] << 0) |
		((u16) ctrl_8_data[1] << 8),
		((u16) ctrl_8_data[2] << 0) | ((u16) ctrl_8_data[3] << 8));

	return 0;
}

static int synaptics_page_description(struct device *dev)
{
	struct synaptics_data *d = to_synaptics_data(dev);
	struct function_descriptor dsc;
	u8 page;

	unsigned short pdt;
	int ret;
	/* TD4100 fw recovery - slave addr change to 0x2c */
	struct i2c_client *client = to_i2c_client(dev);
	u32 backup_slave_addr = client->addr;

	TOUCH_TRACE();

	d->need_fw_recovery = false;
	memset(&d->f01, 0, sizeof(struct synaptics_function));
	memset(&d->f11, 0, sizeof(struct synaptics_function));
	memset(&d->f12, 0, sizeof(struct synaptics_function));
	memset(&d->f1a, 0, sizeof(struct synaptics_function));
	memset(&d->f34, 0, sizeof(struct synaptics_function));
	memset(&d->f51, 0, sizeof(struct synaptics_function));
	memset(&d->f54, 0, sizeof(struct synaptics_function));
	memset(&d->f55, 0, sizeof(struct synaptics_function));

	for (page = 0; page < PAGES_TO_SERVICE; page++) {
		ret = synaptics_set_page(dev, page);

		if (ret < 0) {
			TOUCH_E("faied to set page %d (ret: %d)\n", page, ret);
			/* TD4100 FW recovery - slave addr change to 0x2c */
			TOUCH_E("I2C error - change slave addr to 0x2c\n");
			client->addr = 0x2c;
			synaptics_set_page(dev, page);
		}

		for (pdt = PDT_START; pdt > PDT_END; pdt -= sizeof(dsc)) {
			ret = synaptics_read(dev, pdt, &dsc, sizeof(dsc));

			if (!dsc.fn_number)
				break;

			TOUCH_I("dsc - %02x, %02x, %02x, %02x, %02x, %02x\n",
				dsc.query_base, dsc.command_base,
				dsc.control_base, dsc.data_base,
				dsc.int_source_count, dsc.fn_number);

			switch (dsc.fn_number) {
			case 0x01:
				d->f01.dsc = dsc;
				d->f01.page = page;
				break;

			case 0x11:
				d->f11.dsc = dsc;
				d->f11.page = page;
				break;

			case 0x12:
				d->f12.dsc = dsc;
				d->f12.page = page;
				synaptics_get_f12(dev);
				break;

			case 0x1a:
				d->f1a.dsc = dsc;
				d->f1a.page = page;
				break;

			case 0x34:
				d->f34.dsc = dsc;
				d->f34.page = page;
				break;

				/* TD4100 FW recovery Added */
			case 0x35:
				TOUCH_E("F35 detected - Need FW recovery\n");
				d->need_fw_recovery = true;
				break;

			case 0x51:
				d->f51.dsc = dsc;
				d->f51.page = page;
				break;

			case 0x54:
				d->f54.dsc = dsc;
				d->f54.page = page;
				break;

			default:
				break;
			}
		}
	}

	client->addr = backup_slave_addr;

	TOUCH_I
	    ("common[%dP:0x%02x] finger_f12[%dP:0x%02x] flash[%dP:0x%02x] analog[%dP:0x%02x] lpwg[%dP:0x%02x]\n",
	     d->f01.page, d->f01.dsc.fn_number, d->f12.page, d->f12.dsc.fn_number, d->f34.page,
	     d->f34.dsc.fn_number, d->f54.page, d->f54.dsc.fn_number, d->f51.page,
	     d->f51.dsc.fn_number);
#if 0
	if (!(d->f01.dsc.fn_number &&
	      d->f12.dsc.fn_number &&
	      d->f34.dsc.fn_number && d->f54.dsc.fn_number && d->f51.dsc.fn_number))
		return -EINVAL;
#endif
	ret = synaptics_set_page(dev, DEFAULT_PAGE);

	if (ret) {
		TOUCH_E("faied to set page %d (ret: %d)\n", 0, ret);
		return ret;
	}

	return 0;
}

static void TD4100_ForceUpdate(struct device *dev)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	u8 cmd = 0x04;

	synaptics_set_page(dev, ANALOG_PAGE);
	/* F54_ANALOG_CMD00 */
	synaptics_write(dev, 0x40, &cmd, sizeof(cmd));
	synaptics_set_page(dev, DEFAULT_PAGE);
}

static int synaptics_get_product_id(struct device *dev)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	int ret = 0;

	TOUCH_TRACE();

	ret = synaptics_read(dev, PRODUCT_ID_REG,
			     d->ic_info.product_id, sizeof(d->ic_info.product_id));

	if (ret < 0) {
		TOUCH_I("[%s]read error...\n", __func__);
		return ret;
	}

	TOUCH_I("[%s] IC_product_id: %s\n", __func__, d->ic_info.product_id);

	return 0;
}

int synaptics_ic_info(struct device *dev)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	int ret;

	if (d->need_scan_pdt == true) {
		ret = synaptics_page_description(dev);
		/* After prd.c porting
		   SCAN_PDT(dev);
		 */
		d->need_scan_pdt = false;
	}

	ret = synaptics_get_product_id(dev);
	ret = synaptics_read(dev, FLASH_CONFIG_ID_REG, d->ic_info.raws, sizeof(d->ic_info.raws));
	ret = synaptics_read(dev, CUSTOMER_FAMILY_REG,
			     &(d->ic_info.family), sizeof(d->ic_info.family));
	ret = synaptics_read(dev, FW_REVISION_REG,
			     &(d->ic_info.revision), sizeof(d->ic_info.revision));

	if (ret < 0) {
		TOUCH_I("[%s] read error...\n", __func__);
		return ret;
	}

	d->ic_info.version.major = (d->ic_info.raws[3] & 0x80 ? 1 : 0);
	d->ic_info.version.minor = (d->ic_info.raws[3] & 0x7F);

	TOUCH_I("ic_version = v%d.%02d\n"
		"[Touch] CUSTOMER_FAMILY_REG = %d\n"
		"[Touch] FW_REVISION_REG = %d\n"
		"[Touch] PRODUCT ID = %s\n",
		d->ic_info.version.major, d->ic_info.version.minor,
		d->ic_info.family, d->ic_info.revision, d->ic_info.product_id);

	return 0;
}

static int synaptics_sleep_control(struct device *dev, u8 mode)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);
	u8 val;
	int ret;

	ret = synaptics_read(dev, DEVICE_CONTROL_REG, &val, sizeof(val));

	if (ret < 0) {
		TOUCH_E("failed to read finger report enable - ret:%d\n", ret);
		return ret;
	}

	val &= 0xf8;

	if (mode) {
		val |= 1;
		atomic_set(&ts->state.sleep, IC_DEEP_SLEEP);
	} else {
		val |= 0;
		atomic_set(&ts->state.sleep, IC_NORMAL);
	}

	ret = synaptics_write(dev, DEVICE_CONTROL_REG, &val, sizeof(val));
	if (ret < 0) {
		TOUCH_E("failed to write finger report enable - ret:%d\n", ret);
		return ret;
	}

	TOUCH_I("%s - mode:%d\n", __func__, mode);

	return 0;
}

static int synaptics_lpwg_debug(struct device *dev, int num)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	u8 count = 0;
	u8 index = 0;
	u8 buf = 0;
	u8 i = 0;
	u8 addr = 0;
	u8 offset = num ? LPWG_MAX_BUFFER + 2 : 0;

	synaptics_set_page(dev, LPWG_PAGE);

	synaptics_read(dev, LPWG_TCI1_FAIL_COUNT_REG + offset, &count, sizeof(count));
	synaptics_read(dev, LPWG_TCI1_FAIL_INDEX_REG + offset, &index, sizeof(index));

	for (i = 1; i <= count; i++) {
		addr = LPWG_TCI1_FAIL_BUFFER_REG + offset +
		    ((index + LPWG_MAX_BUFFER - i) % LPWG_MAX_BUFFER);
		synaptics_read(dev, addr, &buf, sizeof(buf));
		TOUCH_I("TCI(%d)-Fail[%d/%d] : %s\n", num, count - i + 1, count,
			(buf > 0 && buf < 6) ? f_str[buf] : f_str[0]);

		if (i == LPWG_MAX_BUFFER)
			break;
	}

	synaptics_set_page(dev, DEFAULT_PAGE);

	return 0;
}

static int synaptics_tci_report_enable(struct device *dev, bool enable)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	u8 val[3];
	int ret;

	ret = synaptics_read(dev, FINGER_REPORT_REG, val, sizeof(val));
	if (ret < 0) {
		TOUCH_E("failed to read finger report enable - ret:%d\n", ret);
		return ret;
	}

	val[2] &= 0xfc;

	if (enable)
		val[2] |= 0x2;

	ret = synaptics_write(dev, FINGER_REPORT_REG, val, sizeof(val));
	if (ret < 0) {
		TOUCH_E("failed to write finger report enable - ret:%d\n", ret);
		return ret;
	}

	TOUCH_I("%s - enable:%d\n", __func__, enable);

	return 0;
}

static void synaptics_sensing_cmd(struct device *dev, int type)
{
	TOUCH_I("%s sensing type = %d\n", __func__, type);

	switch (type) {
	case ABS_STOP:
		synaptics_abs_enable(dev, false);
		break;
	case ABS_SENSING:
		synaptics_abs_enable(dev, true);
		break;
	case WAKEUP_STOP:
		synaptics_tci_report_enable(dev, false);
		break;
	case WAKEUP_SENSING:
		synaptics_abs_enable(dev, false);
		synaptics_tci_report_enable(dev, true);
		break;
	case PARTIAL_WAKEUP_SENSING:
		break;
	default:
		break;
	}
}

static int synaptics_tci_knock(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);
	struct tci_info *info;

	u8 lpwg_data[7];
	int ret;
	u8 tci_reg[2] = { LPWG_TAPCOUNT_REG, LPWG_TAPCOUNT_REG2 };
	int i = 0;

	TOUCH_TRACE();

	ret = synaptics_set_page(dev, LPWG_PAGE);

	if (ret < 0) {
		TOUCH_E("failed to set page to LPWG_PAGE\n");
		return ret;
	}

	for (i = 0; i < 2; i++) {
		if ((ts->tci.mode & (1 << i)) == 0x0) {
			lpwg_data[0] = 0;
			synaptics_write(dev, tci_reg[i], lpwg_data, sizeof(u8));
		} else {
			info = &ts->tci.info[i];
			lpwg_data[0] = ((info->tap_count << 3) | 1);
			lpwg_data[1] = info->min_intertap;
			lpwg_data[2] = info->max_intertap;
			lpwg_data[3] = info->touch_slop;
			lpwg_data[4] = info->tap_distance;
			lpwg_data[6] = (info->intr_delay << 1 | 1);
			synaptics_write(dev, tci_reg[i], lpwg_data, sizeof(lpwg_data));
		}
	}

	ret = synaptics_set_page(dev, DEFAULT_PAGE);

	if (ret < 0) {
		TOUCH_E("failed to set page to DEFAULT_PAGE\n");
		return ret;
	}

	return ret;
}

static int synaptics_tci_password(struct device *dev)
{
	return synaptics_tci_knock(dev);
}

static int synaptics_tci_active_area(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);
	u8 buffer[8];

	buffer[0] = (ts->tci.area.x1 >> 0) & 0xff;
	buffer[1] = (ts->tci.area.x1 >> 8) & 0xff;
	buffer[2] = (ts->tci.area.y1 >> 0) & 0xff;
	buffer[3] = (ts->tci.area.y1 >> 8) & 0xff;
	buffer[4] = (ts->tci.area.x2 >> 0) & 0xff;
	buffer[5] = (ts->tci.area.x2 >> 8) & 0xff;
	buffer[6] = (ts->tci.area.y2 >> 0) & 0xff;
	buffer[7] = (ts->tci.area.y2 >> 8) & 0xff;

	synaptics_write(dev, d->f12_reg.ctrl[18], buffer, sizeof(buffer));

	return 0;
}

static void synaptics_tci_area_set(struct device *dev, int cover_status)
{
	struct touch_core_data *ts = to_touch_core(dev);

	if (cover_status == QUICKCOVER_CLOSE) {
		ts->tci.area.x1 = 179;
		ts->tci.area.y1 = 144;
		ts->tci.area.x2 = 1261;
		ts->tci.area.y2 = 662;
		synaptics_tci_active_area(dev);
		TOUCH_I("LPWG Active Area - QUICKCOVER_CLOSE\n");
	} else {
		ts->tci.area.x1 = 80;
		ts->tci.area.y1 = 0;
		ts->tci.area.x2 = 1359;
		ts->tci.area.y2 = 2479;
		synaptics_tci_active_area(dev);
		TOUCH_I("LPWG Active Area - NORMAL\n");
	}
}

static int synaptics_lpwg_control(struct device *dev, int mode)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct tci_info *info1 = &ts->tci.info[TCI_1];

	switch (mode) {
	case LPWG_DOUBLE_TAP:
		ts->tci.mode = 0x01;
		info1->intr_delay = 0;
		info1->tap_distance = 10;
		synaptics_tci_knock(dev);
		TD4100_ForceUpdate(dev);
		break;

	case LPWG_PASSWORD:
		ts->tci.mode = 0x03;
		info1->intr_delay = ts->tci.double_tap_check ? 68 : 0;
		info1->tap_distance = 7;
		synaptics_tci_password(dev);
		TD4100_ForceUpdate(dev);
		break;

	default:
		ts->tci.mode = 0;
		synaptics_tci_knock(dev);
		break;
	}

	return 0;
}

static int synaptics_lpwg_mode(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);

	if (atomic_read(&ts->state.fb) == FB_SUSPEND) {
		if (ts->lpwg.screen) {
			TOUCH_I("%s(%d) - FB_SUSPEND & screen on -> skip\n", __func__, __LINE__);
			if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP)
				synaptics_sleep_control(dev, IC_NORMAL);
			synaptics_sensing_cmd(dev, WAKEUP_STOP);
			synaptics_lpwg_debug(dev, TCI_1);
			synaptics_lpwg_debug(dev, TCI_2);
		} else if (ts->lpwg.mode == LPWG_NONE) {
			/* deep sleep */
			TOUCH_I("%s(%d) - deep sleep\n", __func__, __LINE__);
			if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP)
				synaptics_sleep_control(dev, IC_NORMAL);
			synaptics_lpwg_control(dev, LPWG_NONE);
			synaptics_sleep_control(dev, IC_DEEP_SLEEP);
		} else if (ts->lpwg.sensor == PROX_NEAR) {
			/* deep sleep */
			TOUCH_I("%s(%d) - deep sleep by prox\n", __func__, __LINE__);
			if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP)
				synaptics_sleep_control(dev, IC_NORMAL);
			synaptics_lpwg_control(dev, LPWG_NONE);
			synaptics_sleep_control(dev, IC_DEEP_SLEEP);
		} else if (ts->lpwg.qcover == HOLE_NEAR) {
			/* knock on */
			TOUCH_I("%s(%d) - knock on by hole\n", __func__, __LINE__);
			if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP)
				synaptics_sleep_control(dev, IC_NORMAL);
			synaptics_tci_area_set(dev, QUICKCOVER_CLOSE);
			synaptics_lpwg_control(dev, LPWG_DOUBLE_TAP);
			synaptics_sensing_cmd(dev, WAKEUP_SENSING);
		} else {
			/* knock on/code */
			if (atomic_read(&ts->state.sleep) == IC_DEEP_SLEEP)
				synaptics_sleep_control(dev, IC_NORMAL);
			synaptics_tci_area_set(dev, QUICKCOVER_OPEN);
			synaptics_lpwg_control(dev, ts->lpwg.mode);
			synaptics_sensing_cmd(dev, WAKEUP_SENSING);
		}
		return 0;
	}

	/* resume */
	touch_report_all_event(ts);
	if (ts->lpwg.screen) {
		/* normal */
		TOUCH_I("%s(%d) - normal\n", __func__, __LINE__);
		synaptics_lpwg_control(dev, LPWG_NONE);
		synaptics_sensing_cmd(dev, ABS_SENSING);
	} else if (ts->lpwg.mode == LPWG_NONE) {
		/* normal */
		TOUCH_I("%s(%d) - normal on screen off\n", __func__, __LINE__);
		synaptics_lpwg_control(dev, LPWG_NONE);
		synaptics_sensing_cmd(dev, ABS_STOP);
	} else if (ts->lpwg.sensor == PROX_NEAR) {
		/* wake up */
		TOUCH_I("%s(%d) - wake up on screen off and prox\n", __func__, __LINE__);
		synaptics_lpwg_control(dev, LPWG_NONE);
		synaptics_sensing_cmd(dev, ABS_STOP);
	} else {
		/* partial */
		TOUCH_I("%s - partial is not ready\n", __func__);
		if (ts->lpwg.qcover == HOLE_NEAR)
			synaptics_tci_area_set(dev, QUICKCOVER_CLOSE);
		else
			synaptics_tci_area_set(dev, QUICKCOVER_OPEN);
		synaptics_lpwg_control(dev, LPWG_DOUBLE_TAP);
		synaptics_sensing_cmd(dev, PARTIAL_WAKEUP_SENSING);
	}

	return 0;
}

static void synaptics_init_tci_info(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);

	ts->tci.info[TCI_1].tap_count = 2;
	ts->tci.info[TCI_1].min_intertap = 0;
	ts->tci.info[TCI_1].max_intertap = 70;
	ts->tci.info[TCI_1].touch_slop = 100;
	ts->tci.info[TCI_1].tap_distance = 10;
	ts->tci.info[TCI_1].intr_delay = 0;

	ts->tci.info[TCI_2].min_intertap = 0;
	ts->tci.info[TCI_2].max_intertap = 70;
	ts->tci.info[TCI_2].touch_slop = 100;
	ts->tci.info[TCI_2].tap_distance = 255;
	ts->tci.info[TCI_2].intr_delay = 20;
}

static int synaptics_remove(struct device *dev)
{
	if (rmidev_fhandler.initialized && rmidev_fhandler.insert) {
		rmidev_fhandler.exp_fn->remove(dev);
		rmidev_fhandler.initialized = false;
	}

	return 0;
}

static int synaptics_get_status(struct device *dev, u8 *interrupt)
{
	struct synaptics_data *d = to_synaptics_data(dev);
	u8 status[2] = { 0, };
	int ret;

	ret = synaptics_read(dev, DEVICE_STATUS_REG, &status, sizeof(status));

	if (ret < 0) {
		TOUCH_E("failed to read device status - ret:%d\n", ret);
		return ret;
	}

	TOUCH_D(TRACE, "status[device:%02x, interrupt:%02x]\n", status[0], status[1]);

	if (interrupt)
		*interrupt = status[1];

	return ret;
}

static int synaptics_irq_clear(struct device *dev)
{
	return synaptics_get_status(dev, NULL);
}

static int synaptics_noise_log(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);

	u8 buffer[2] = { 0 };
	u8 buf_lsb = 0, buf_msb = 0, cns = 0;
	u16 im = 0, cid_im = 0, freq_scan_im = 0;

	synaptics_set_page(dev, ANALOG_PAGE);
	synaptics_read(dev, INTERFERENCE_METRIC_LSB_REG, &buf_lsb, sizeof(buf_lsb));
	synaptics_read(dev, INTERFERENCE_METRIC_MSB_REG, &buf_msb, sizeof(buf_msb));

	im = (buf_msb << 8) | buf_lsb;
	d->noise.im_sum += im;

	synaptics_read(dev, CURRENT_NOISE_STATUS_REG, &cns, sizeof(cns));
	d->noise.cns_sum += cns;

	synaptics_read(dev, CID_IM_REG, buffer, sizeof(buffer));
	cid_im = (buffer[1] << 8) | buffer[0];
	d->noise.cid_im_sum += cid_im;

	synaptics_read(dev, FREQ_SCAN_IM_REG, buffer, sizeof(buffer));
	freq_scan_im = (buffer[1] << 8) | buffer[0];
	d->noise.freq_scan_im_sum += freq_scan_im;
	synaptics_set_page(dev, DEFAULT_PAGE);

	d->noise.cnt++;

	if (d->noise.noise_log == NOISE_ENABLE) {
		if (ts->old_mask != ts->new_mask) {
			TOUCH_I("Curr : CNS[%5d] IM[%5d] CID_IM[%5d] FREQ_SCAN_IM[%5d]\n",
				cns, im, cid_im, freq_scan_im);
		}
	}

	if (ts->new_mask == 0 || (d->noise.im_sum >= ULONG_MAX
				  || d->noise.cns_sum >= ULONG_MAX
				  || d->noise.cid_im_sum >= ULONG_MAX
				  || d->noise.freq_scan_im_sum >= ULONG_MAX
				  || d->noise.cnt >= UINT_MAX)) {
		if (d->noise.noise_log == NOISE_ENABLE) {
			TOUCH_I
			    ("Aver : CNS[%5lu] IM[%5lu] CID_IM[%5lu] FREQ_SCAN_IM[%5lu] (cnt:%u)\n",
			     d->noise.cns_sum / d->noise.cnt, d->noise.im_sum / d->noise.cnt,
			     d->noise.cid_im_sum / d->noise.cnt,
			     d->noise.freq_scan_im_sum / d->noise.cnt, d->noise.cnt);
		}

		d->noise.im_avg = d->noise.im_sum / d->noise.cnt;
		d->noise.cns_avg = d->noise.cns_sum / d->noise.cnt;
		d->noise.cid_im_avg = d->noise.cid_im_sum / d->noise.cnt;
		d->noise.freq_scan_im_avg = d->noise.freq_scan_im_sum / d->noise.cnt;
	}

	if (ts->old_mask == 0 && ts->new_mask != 0) {
		d->noise.cnt = d->noise.im_sum = d->noise.cns_sum =
		    d->noise.cid_im_sum = d->noise.freq_scan_im_sum = 0;
		d->noise.im_avg = d->noise.cns_avg =
		    d->noise.cid_im_avg = d->noise.freq_scan_im_avg = 0;
	}

	return 0;
}

static int synaptics_get_object_count(struct device *dev)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	u8 object_to_read = d->num_of_fingers;
	u8 buf[2] = { 0, };
	u16 object_attention = 0;
	int ret;

	ret = synaptics_read(dev, d->f12_reg.data[15], (u8 *) buf, sizeof(buf));

	if (ret < 0) {
		TOUCH_E("%s, %d : get object_attention data failed\n", __func__, __LINE__);
		return ret;
	}

	object_attention = (((u16) ((buf[1] << 8) & 0xFF00) | (u16) ((buf[0]) & 0xFF)));

	for (; object_to_read > 0; object_to_read--) {
		if (object_attention & (0x1 << (object_to_read - 1)))
			break;
	}
	TOUCH_D(ABS, "object_to_read: %d\n", object_to_read);

	return object_to_read;
}

static int synaptics_irq_abs_data(struct device *dev, int object_to_read)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);
	struct synaptics_object objects[MAX_NUM_OF_FINGERS];
	struct synaptics_object *obj;
	struct touch_data *tdata;
	u8 finger_index = 0;
	int ret = 0;
	int i = 0;

	ts->new_mask = 0;

	if (object_to_read == 0)
		goto end;

	ret = synaptics_read(dev, FINGER_DATA_REG, objects, sizeof(*obj) * object_to_read);
	if (ret < 0) {
		TOUCH_E("faied to read finger data\n");
		goto end;
	}

	for (i = 0; i < object_to_read; i++) {
		obj = objects + i;

		if (obj->type == F12_NO_OBJECT_STATUS)
			continue;

		if (obj->type > F12_MAX_OBJECT)
			TOUCH_D(ABS, "id : %d, type : %d\n", i, obj->type);

		if (obj->type == F12_FINGER_STATUS) {
			ts->new_mask |= (1 << i);
			tdata = ts->tdata + i;

			tdata->id = i;
			tdata->type = obj->type;
			tdata->x = obj->x_lsb | obj->x_msb << 8;
			tdata->y = obj->y_lsb | obj->y_msb << 8;
			tdata->pressure = obj->z;

			if (obj->wx > obj->wy) {
				tdata->width_major = obj->wx;
				tdata->width_minor = obj->wy;
				tdata->orientation = 0;
			} else {
				tdata->width_major = obj->wy;
				tdata->width_minor = obj->wx;
				tdata->orientation = 1;
			}

			finger_index++;

			TOUCH_D(ABS,
				"tdata [id:%d t:%d x:%d y:%d z:%d-%d,%d,%d]\n",
				tdata->id,
				tdata->type,
				tdata->x,
				tdata->y,
				tdata->pressure,
				tdata->width_major, tdata->width_minor, tdata->orientation);
		}
	}

	if (d->noise.check_noise == NOISE_ENABLE || d->noise.noise_log == NOISE_ENABLE)
		synaptics_noise_log(dev);

end:
	ts->tcount = finger_index;
	ts->intr_status = TOUCH_IRQ_FINGER;

	return ret;
}

static int synaptics_irq_abs(struct device *dev)
{
	u8 object_to_read = 0;
	int ret = 0;

	object_to_read = synaptics_get_object_count(dev);

	if (object_to_read < 0) {
		TOUCH_E("faied to read object count\n");
		return ret;
	}

	return synaptics_irq_abs_data(dev, object_to_read);
}

static int synaptics_tci_getdata(struct device *dev, int count)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);
	u32 buffer[12];
	int i = 0;
	int ret;

	ts->lpwg.code_num = count;

	if (!count)
		return 0;

	ret = synaptics_read(dev, LPWG_DATA_REG, buffer, sizeof(u32) * count);

	if (ret < 0)
		return ret;

	for (i = 0; i < count; i++) {
		ts->lpwg.code[i].x = buffer[i] & 0xffff;
		ts->lpwg.code[i].y = (buffer[i] >> 16) & 0xffff;

		/* temp - there is not overtap point */
		if (buffer[i] == 0) {
			ts->lpwg.code[i].x = 1;
			ts->lpwg.code[i].y = 1;
		}

		if ((ts->lpwg.mode == LPWG_PASSWORD) && (ts->role.hide_coordinate))
			TOUCH_I("LPWG data xxxx, xxxx\n");
		else
			TOUCH_I("LPWG data %d, %d\n", ts->lpwg.code[i].x, ts->lpwg.code[i].y);
	}
	ts->lpwg.code[count].x = -1;
	ts->lpwg.code[count].y = -1;

	return 0;
}

static int synaptics_irq_lpwg(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);
	u8 status;
	u8 buffer;
	int ret;

	ret = synaptics_set_page(dev, LPWG_PAGE);

	if (ret < 0) {
		synaptics_set_page(dev, DEFAULT_PAGE);
		return ret;
	}

	ret = synaptics_read(dev, LPWG_STATUS_REG, &status, 1);
	if (ret < 0) {
		synaptics_set_page(dev, DEFAULT_PAGE);
		return ret;
	}

	if (status & LPWG_STATUS_DOUBLETAP) {
		synaptics_tci_getdata(dev, ts->tci.info[TCI_1].tap_count);
		ts->intr_status = TOUCH_IRQ_KNOCK;
	} else if (status & LPWG_STATUS_PASSWORD) {
		synaptics_tci_getdata(dev, ts->tci.info[TCI_2].tap_count);
		ts->intr_status = TOUCH_IRQ_PASSWD;
	} else {
		/* Overtab */
		synaptics_read(dev, LPWG_OVER_TAPCOUNT, &buffer, 1);
		if (buffer > ts->tci.info[TCI_2].tap_count) {
			synaptics_tci_getdata(dev, ts->tci.info[TCI_2].tap_count + 1);
			ts->intr_status = TOUCH_IRQ_PASSWD;
			TOUCH_I("knock code fail to over tap count = %d\n", buffer);
		}
	}

	return synaptics_set_page(dev, DEFAULT_PAGE);
}

static int synaptics_irq_handler(struct device *dev)
{
	u8 irq_status;
	int ret = 0;

	TOUCH_TRACE();

	ret = synaptics_get_status(dev, &irq_status);

	if (irq_status & INTERRUPT_MASK_ABS0)
		ret = synaptics_irq_abs(dev);
	else if (irq_status & INTERRUPT_MASK_LPWG)
		ret = synaptics_irq_lpwg(dev);

	return ret;
}

void synaptics_rmidev_function(struct synaptics_rmidev_exp_fn *exp_fn, bool insert)
{
	TOUCH_TRACE();

	rmidev_fhandler.insert = insert;

	if (insert) {
		rmidev_fhandler.exp_fn = exp_fn;
		rmidev_fhandler.insert = true;
		rmidev_fhandler.remove = false;
	} else {
		rmidev_fhandler.exp_fn = NULL;
		rmidev_fhandler.insert = false;
		rmidev_fhandler.remove = true;
	}
}

static int synaptics_rmidev_init(struct device *dev)
{
	int ret = 0;

	TOUCH_TRACE();

	if (rmidev_fhandler.insert) {	/* TODO DEBUG_OPTION_2 */
		ret = rmidev_fhandler.exp_fn->init(dev);

		if (ret < 0)
			TOUCH_I("%s : Failed to init rmi_dev settings\n", __func__);
		else
			rmidev_fhandler.initialized = true;
	}

	return 0;
}

int synaptics_init(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);

	synaptics_ic_info(dev);

	/* disable abs */
	synaptics_abs_enable(dev, true);
	/* after irq_clear, interrupt occurred */
	synaptics_irq_clear(dev);
	atomic_set(&ts->state.sleep, IC_NORMAL);

	synaptics_lpwg_mode(dev);
	synaptics_rmidev_init(dev);

	return 0;
}

static int synaptics_power(struct device *dev, int ctrl)
{
	struct touch_core_data *ts = to_touch_core(dev);

	TOUCH_TRACE();

	switch (ctrl) {
	case POWER_OFF:
		if (atomic_read(&ts->state.core) == CORE_PROBE) {
			TOUCH_I("%s, off\n", __func__);
			touch_gpio_direction_output(ts->reset_pin, 0);
			touch_power_vio(dev, 0);
			touch_power_vdd(dev, 0);
			touch_msleep(1);
		}
		break;

	case POWER_ON:
		if (atomic_read(&ts->state.core) == CORE_PROBE) {
			TOUCH_I("%s, on\n", __func__);
			touch_power_vdd(dev, 1);
			touch_power_vio(dev, 1);
			touch_gpio_direction_output(ts->reset_pin, 1);
		}
		break;

	case POWER_SLEEP:
		TOUCH_I("%s, sleep\n", __func__);
		break;

	case POWER_WAKE:
		TOUCH_I("%s, wake\n", __func__);
		break;
	}

	return 0;
}

static int synaptics_suspend(struct device *dev)
{
	TOUCH_TRACE();

	if (touch_boot_mode() == TOUCH_CHARGER_MODE) {
		TOUCH_I("%s : Charger mode!!!\n", __func__);
		return -EPERM;
	}

	synaptics_lpwg_mode(dev);

	return 0;
}

static int synaptics_resume(struct device *dev)
{
	TOUCH_TRACE();

	if (touch_boot_mode() == TOUCH_CHARGER_MODE) {
		TOUCH_I("%s : Charger mode!!!\n", __func__);
		/* Deep Sleep */
		synaptics_sleep_control(dev, 1);
		return -EPERM;
	}

	touch_interrupt_control(dev, INTERRUPT_DISABLE);
	synaptics_reset_ctrl(dev, SW_RESET);

	return 0;
}

static int synaptics_lpwg(struct device *dev, u32 code, void *param)
{
	struct touch_core_data *ts = to_touch_core(dev);

	int *value = (int *)param;

	TOUCH_TRACE();

	switch (code) {
	case LPWG_ACTIVE_AREA:
		ts->tci.area.x1 = value[0];
		ts->tci.area.x2 = value[1];
		ts->tci.area.y1 = value[2];
		ts->tci.area.y2 = value[3];
		TOUCH_I("LPWG_ACTIVE_AREA: x0[%d], x1[%d], x2[%d], x3[%d]\n",
			value[0], value[1], value[2], value[3]);
		break;

	case LPWG_TAP_COUNT:
		ts->tci.info[TCI_2].tap_count = value[0];
		break;

	case LPWG_DOUBLE_TAP_CHECK:
		ts->tci.double_tap_check = value[0];
		break;

	case LPWG_UPDATE_ALL:
		ts->lpwg.mode = value[0];
		ts->lpwg.screen = value[1];
		ts->lpwg.sensor = value[2];
		ts->lpwg.qcover = value[3];
		TOUCH_I("LPWG_UPDATE_ALL: mode[%d], screen[%s], sensor[%s], qcover[%s]\n",
			ts->lpwg.mode,
			ts->lpwg.screen ? "ON" : "OFF",
			ts->lpwg.sensor ? "FAR" : "NEAR", ts->lpwg.qcover ? "CLOSE" : "OPEN");
		synaptics_lpwg_mode(dev);
		break;
	}

	return 0;
}

static int synaptics_bin_fw_version(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);

	const struct firmware *fw = NULL;
	int rc = 0;

	rc = request_firmware(&fw, ts->def_fwpath[0], dev);
	if (rc != 0) {
		TOUCH_E("[%s] request_firmware() failed %d\n", __func__, rc);
		return -EIO;
	}

	memcpy(d->ic_info.img_product_id, &fw->data[d->ic_info.fw_pid_addr], 6);
	memcpy(d->ic_info.img_raws, &fw->data[d->ic_info.fw_ver_addr], 4);
	d->ic_info.img_version.major = (d->ic_info.img_raws[3] & 0x80 ? 1 : 0);
	d->ic_info.img_version.minor = (d->ic_info.img_raws[3] & 0x7F);

	release_firmware(fw);

	return rc;
}

static char *synaptics_productcode_parse(unsigned char *product)
{
	static char str[128] = { 0 };
	int len = 0;
	char inch[2] = { 0 };
	char paneltype = 0;
	char version[2] = { 0 };
	const char *str_panel[]
	= { "ELK", "Suntel", "Tovis", "Innotek", "JDI", "LGD", };
	static const char * const str_ic[] = { "Synaptics", };
	int i;

	i = (product[0] & 0xF0) >> 4;
	if (i < 6)
		len += snprintf(str + len, sizeof(str) - len, "%s\n", str_panel[i]);
	else
		len += snprintf(str + len, sizeof(str) - len, "Unknown\n");

	i = (product[0] & 0x0F);
	if (i < 5 && i != 1)
		len += snprintf(str + len, sizeof(str) - len, "%dkey\n", i);
	else
		len += snprintf(str + len, sizeof(str) - len, "Unknown\n");

	i = (product[1] & 0xF0) >> 4;
	if (i < 1)
		len += snprintf(str + len, sizeof(str) - len, "%s\n", str_ic[i]);
	else
		len += snprintf(str + len, sizeof(str) - len, "Unknown\n");

	inch[0] = (product[1] & 0x0F);
	inch[1] = ((product[2] & 0xF0) >> 4);
	len += snprintf(str + len, sizeof(str) - len, "%d.%d\n", inch[0], inch[1]);

	paneltype = (product[2] & 0x0F);
	len += snprintf(str + len, sizeof(str) - len, "PanelType %d\n", paneltype);

	version[0] = ((product[3] & 0x80) >> 7);
	version[1] = (product[3] & 0x7F);
	len += snprintf(str + len, sizeof(str) - len,
			"version : v%d.%02d\n", version[0], version[1]);

	return str;
}

static int synaptics_get_cmd_version(struct device *dev, char *buf)
{
	struct synaptics_data *d = to_synaptics_data(dev);
	int offset = 0;
	int ret = 0;

	ret = synaptics_ic_info(dev);
	ret += synaptics_bin_fw_version(dev);

	if (ret < 0) {
		offset += snprintf(buf + offset, PAGE_SIZE, "-1\n");
		offset += snprintf(buf + offset, PAGE_SIZE - offset, "Read Fail Touch IC Info\n");
		return offset;
	}

	offset = snprintf(buf + offset, PAGE_SIZE - offset, "\n======== Firmware Info ========\n");
	/* IC_Info */
	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			   "ic_version RAW = %02X %02X %02X %02X\n",
			   d->ic_info.raws[0], d->ic_info.raws[1],
			   d->ic_info.raws[2], d->ic_info.raws[3]);
	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			   "=== ic_fw_version info ===\n%s",
			   synaptics_productcode_parse(d->ic_info.raws));
	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			   "IC_product_id[%s]\n", d->ic_info.product_id);
	if (synaptics_is_product(d, "PLG623", 6))
		offset += snprintf(buf + offset, PAGE_SIZE - offset, "Touch IC : TD4100\n\n");
	else
		offset += snprintf(buf + offset, PAGE_SIZE - offset,
				   "Touch product ID read fail\n");

	/* Image_Info */
	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			   "img_version RAW = %02X %02X %02X %02X\n",
			   d->ic_info.img_raws[0], d->ic_info.img_raws[1],
			   d->ic_info.img_raws[2], d->ic_info.img_raws[3]);
	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			   "=== img_version info ===\n%s",
			   synaptics_productcode_parse(d->ic_info.img_raws));
	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			   "Img_product_id[%s]\n", d->ic_info.img_product_id);
	if (synaptics_is_img_product(d, "PLG623", 6))
		offset += snprintf(buf + offset, PAGE_SIZE - offset, "Touch IC : TD4100\n\n");
	else
		offset += snprintf(buf + offset, PAGE_SIZE - offset,
				   "Touch product ID read fail\n");

	return offset;
}

static int synaptics_get_cmd_atcmd_version(struct device *dev, char *buf)
{
	struct synaptics_data *d = to_synaptics_data(dev);
	int offset = 0;
	int ret = 0;

	ret = synaptics_ic_info(dev);
	if (ret < 0) {
		offset += snprintf(buf + offset, PAGE_SIZE, "-1\n");
		offset += snprintf(buf + offset, PAGE_SIZE - offset, "Read Fail Touch IC Info\n");
		return offset;
	}

	offset = snprintf(buf + offset, PAGE_SIZE - offset,
			  "v%d.%02d(0x%X/0x%X/0x%X/0x%X)\n",
			  d->ic_info.version.major,
			  d->ic_info.version.minor,
			  d->ic_info.raws[0],
			  d->ic_info.raws[1], d->ic_info.raws[2], d->ic_info.raws[3]);

	return offset;
}

static int synaptics_debug_option(struct device *dev, u32 *data)
{
	u32 chg_mask = data[0];
	u32 enable = data[1];

	switch (chg_mask) {
	case DEBUG_OPTION_0:
		TOUCH_I("Debug Option 0 %s\n", enable ? "Enable" : "Disable");
		break;
	default:
		TOUCH_E("Not supported debug option\n");
		break;
	}

	return 0;
}

static int synaptics_notify(struct device *dev, ulong event, void *data)
{
	int ret = 0;

	TOUCH_TRACE();

	switch (event) {
	case NOTIFY_DEBUG_OPTION:
		TOUCH_I("NOTIFY_DEBUG_OPTION!\n");
		ret = synaptics_debug_option(dev, (u32 *) data);
		break;
	}

	return ret;
}

static ssize_t show_pen_support(struct device *dev, char *buf)
{
	int ret = 0;

	ret += snprintf(buf + ret, PAGE_SIZE - ret, "%d\n", 1);

	return ret;
}

static ssize_t store_reg_ctrl(struct device *dev, const char *buf, size_t count)
{
	struct touch_core_data *ts = to_touch_core(dev);
	u8 buffer[50] = { 0 };
	char command[6] = { 0 };
	int page = 0;
	u32 reg = 0;
	int offset = 0;
	u32 value = 0;

	if (sscanf(buf, "%5s %d %x %d %x ", command, &page, &reg, &offset, &value) <= 0)
		return count;

	if ((offset < 0) || (offset > 49)) {
			TOUCH_E("invalid offset[%d]\n", offset);
				return count;
	}

	mutex_lock(&ts->lock);
	synaptics_set_page(dev, page);
	if (!strcmp(command, "write")) {
		synaptics_read(dev, reg, buffer, offset + 1);
		buffer[offset] = (u8) value;
		synaptics_write(dev, reg, buffer, offset + 1);
	} else if (!strcmp(command, "read")) {
		synaptics_read(dev, reg, buffer, offset + 1);
		TOUCH_I("page[%d] reg[%x] offset[%d] = 0x%x\n", page, reg, offset, buffer[offset]);
	} else {
		TOUCH_E("Usage\n");
		TOUCH_E("Write page reg offset value\n");
		TOUCH_E("Read page reg offset\n");
	}
	synaptics_set_page(dev, DEFAULT_PAGE);
	mutex_unlock(&ts->lock);
	return count;
}

static ssize_t show_check_noise(struct device *dev, char *buf)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	int offset = 0;

	offset += snprintf(buf + offset, PAGE_SIZE - offset, "Test Count : %d\n", d->noise.cnt);
	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			   "Current Noise State : %d\n", d->noise.cns_avg);
	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			   "Interference Metric : %d\n", d->noise.im_avg);
	offset += snprintf(buf + offset, PAGE_SIZE - offset, "CID IM : %d\n", d->noise.cid_im_avg);
	offset += snprintf(buf + offset, PAGE_SIZE - offset,
			   "Freq Scan IM : %d\n", d->noise.freq_scan_im_avg);

	return offset;
}

static ssize_t store_check_noise(struct device *dev, const char *buf, size_t count)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	int value = 0;
	int ret;

	ret = kstrtoint(buf, 0, &value);
	if (ret != 0)
		return count;

	if ((d->noise.check_noise == NOISE_DISABLE)
	    && (value == NOISE_ENABLE)) {
		d->noise.check_noise = NOISE_ENABLE;
	} else if ((d->noise.check_noise == NOISE_ENABLE)
		   && (value == NOISE_DISABLE)) {
		d->noise.check_noise = NOISE_DISABLE;
	} else {
		TOUCH_I("Already enabled check_noise\n");
		TOUCH_I("check_noise = %d, value = %d\n", d->noise.check_noise, value);
		return count;
	}

	TOUCH_I("check_noise = %s\n", (d->noise.check_noise == NOISE_ENABLE)
		? "NOISE_CHECK_ENABLE" : "NOISE_CHECK_DISABLE");

	return count;
}

static ssize_t show_noise_log(struct device *dev, char *buf)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	int offset = 0;

	offset += snprintf(buf + offset, PAGE_SIZE - offset, "%d\n", d->noise.noise_log);

	TOUCH_I("noise_log = %s\n", (d->noise.noise_log == NOISE_ENABLE)
		? "NOISE_LOG_ENABLE" : "NOISE_LOG_DISABLE");

	return offset;
}

static ssize_t store_noise_log(struct device *dev, const char *buf, size_t count)
{
	struct synaptics_data *d = to_synaptics_data(dev);

	int value = 0;
	int ret;

	ret = kstrtoint(buf, 0, &value);
	if (ret != 0)
		return count;

	if ((d->noise.noise_log == NOISE_DISABLE)
	    && (value == NOISE_ENABLE)) {
		d->noise.noise_log = NOISE_ENABLE;
	} else if ((d->noise.noise_log == NOISE_ENABLE)
		   && (value == NOISE_DISABLE)) {
		d->noise.noise_log = NOISE_DISABLE;
	} else {
		TOUCH_I("Already enabled noise_log\n");
		TOUCH_I("noise_log = %d, value = %d\n", d->noise.noise_log, value);
		return count;
	}

	TOUCH_I("noise_log = %s\n", (d->noise.noise_log == NOISE_ENABLE)
		? "NOISE_LOG_ENABLE" : "NOISE_LOG_DISABLE");

	return count;
}

static int synaptics_recovery(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct i2c_client *client = to_i2c_client(dev);
	const struct firmware *fw = NULL;
	char fwpath[256] = { 0 };
	u32 backup_slave_addr = client->addr;
	int ret = 0;

	memcpy(fwpath, ts->def_fwpath[1], sizeof(fwpath));
	if (fwpath == NULL) {
		TOUCH_E("error get fw path\n");
		return -EPERM;
	}
	TOUCH_I("fwpath[%s]\n", fwpath);

	ret = request_firmware(&fw, fwpath, dev);
	if (ret < 0) {
		TOUCH_E("fail to request_firmware fwpath: %s (ret:%d)\n", fwpath, ret);
		return ret;
	}
	TOUCH_I("fw size:%zu, data: %p\n", fw->size, fw->data);

	client->addr = 0x2c;
	FirmwareRecovery(dev, fw);
	client->addr = backup_slave_addr;

	release_firmware(fw);

	return ret;
}

static ssize_t show_fw_recovery(struct device *dev, char *buf)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);
	int offset = 0;

	atomic_set(&ts->state.core, CORE_UPGRADE);
	mutex_lock(&ts->lock);
	touch_interrupt_control(ts->dev, INTERRUPT_DISABLE);

	synaptics_recovery(dev);

	d->need_scan_pdt = true;
	/* init force_upgrade */
	ts->force_fwup = 0;
	ts->test_fwpath[0] = '\0';

	synaptics_reset_ctrl(dev, SW_RESET);
	synaptics_init(dev);
	atomic_set(&ts->state.core, CORE_NORMAL);
	touch_interrupt_control(ts->dev, INTERRUPT_ENABLE);
	mutex_unlock(&ts->lock);

	offset += snprintf(buf + offset, PAGE_SIZE - offset, "%s\n", "FirmwareRecovery Finish");

	return offset;
}

static ssize_t store_reset_ctrl(struct device *dev, const char *buf, size_t count)
{
	int value = 0;
	int ret;

	ret = kstrtoint(buf, 0, &value);
	if (ret != 0)
		return count;
	touch_interrupt_control(dev, INTERRUPT_DISABLE);
	synaptics_reset_ctrl(dev, value);
	synaptics_init(dev);
	touch_interrupt_control(dev, INTERRUPT_ENABLE);
	return count;
}

static TOUCH_ATTR(reg_ctrl, NULL, store_reg_ctrl);
static TOUCH_ATTR(pen_support, show_pen_support, NULL);
static TOUCH_ATTR(ts_noise, show_check_noise, store_check_noise);
static TOUCH_ATTR(ts_noise_log_enable, show_noise_log, store_noise_log);
static TOUCH_ATTR(fw_recovery, show_fw_recovery, NULL);
static TOUCH_ATTR(reset_ctrl, NULL, store_reset_ctrl);


static struct attribute *td4100_attribute_list[] = {
	&touch_attr_reg_ctrl.attr,
	&touch_attr_pen_support.attr,
	&touch_attr_ts_noise.attr,
	&touch_attr_ts_noise_log_enable.attr,
	&touch_attr_fw_recovery.attr,
	&touch_attr_reset_ctrl.attr,
	NULL,
};

static const struct attribute_group td4100_attribute_group = {
	.attrs = td4100_attribute_list,
};

static int synaptics_register_sysfs(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	int ret = 0;

	TOUCH_TRACE();

	ret = sysfs_create_group(&ts->kobj, &td4100_attribute_group);
	if (ret < 0)
		TOUCH_E("td4100 sysfs register failed\n");

	td4100_prd_register_sysfs(dev);

	return 0;
}

static int synaptics_set(struct device *dev, u32 cmd, void *input, void *output)
{
	TOUCH_TRACE();

	return 0;
}

static int synaptics_get(struct device *dev, u32 cmd, void *input, void *output)
{
	int ret = 0;

	TOUCH_D(BASE_INFO, "%s : cmd %d\n", __func__, cmd);

	switch (cmd) {
	case CMD_VERSION:
		ret = synaptics_get_cmd_version(dev, (char *)output);
		break;

	case CMD_ATCMD_VERSION:
		ret = synaptics_get_cmd_atcmd_version(dev, (char *)output);
		break;

	default:
		break;
	}

	return ret;
}

static int synaptics_fw_compare(struct device *dev, const struct firmware *fw)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);
	struct synaptics_version *device = &d->ic_info.version;
	struct synaptics_version *binary = NULL;
	int update = 0;

	memcpy(d->ic_info.img_product_id, &fw->data[d->ic_info.fw_pid_addr], 6);
	memcpy(d->ic_info.img_raws, &fw->data[d->ic_info.fw_ver_addr], 4);
	d->ic_info.img_version.major = (d->ic_info.img_raws[3] & 0x80 ? 1 : 0);
	d->ic_info.img_version.minor = (d->ic_info.img_raws[3] & 0x7F);
	binary = &d->ic_info.img_version;

	if (ts->force_fwup)
		update = 1;

	/*else if (binary->major && device->major) {
	   if (binary->minor != device->minor)
	   update = 1;
	   else if (binary->build > device->build)
	   update = 1;
	   } else if (binary->major ^ device->major) {
	   update = 0;
	   } else if (!binary->major && !device->major) {
	   if (binary->minor > device->minor)
	   update = 1;
	   } */
	TOUCH_I("%s : binary[%d.%02d.%d] device[%d.%02d.%d] -> update: %d, force: %d\n",
		__func__,
		binary->major, binary->minor, binary->build,
		device->major, device->minor, device->build, update, ts->force_fwup);

	return update;
}

static int synaptics_upgrade(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = to_synaptics_data(dev);
	const struct firmware *fw = NULL;
	char fwpath[256] = { 0 };
	int ret = 0;
	int i = 0;

	if (ts->test_fwpath[0]) {
		memcpy(fwpath, &ts->test_fwpath[0], sizeof(fwpath));
		TOUCH_I("get fwpath from test_fwpath:%s\n", &ts->test_fwpath[0]);
	} else if (ts->def_fwcnt) {
		/* TD4100 fw_recovery */
		if (d->need_fw_recovery) {
			d->need_fw_recovery = false;
			if (synaptics_recovery(dev) < 0) {
				TOUCH_E("fw_recovery fail\n");
				return -EPERM;
			}
			ts->force_fwup = 1;
			touch_msleep(20);
		}

		memcpy(fwpath, ts->def_fwpath[0], sizeof(fwpath));
	} else {
		TOUCH_E("no firmware file\n");
		return -EPERM;
	}

	if (fwpath == NULL) {
		TOUCH_E("error get fw path\n");
		return -EPERM;
	}

	TOUCH_I("fwpath[%s]\n", fwpath);

	ret = request_firmware(&fw, fwpath, dev);

	if (ret < 0) {
		TOUCH_E("fail to request_firmware fwpath: %s (ret:%d)\n", fwpath, ret);

		return ret;
	}

	TOUCH_I("fw size:%zu, data: %p\n", fw->size, fw->data);

	if (synaptics_fw_compare(dev, fw)) {
		d->need_scan_pdt = true;
		for (i = 0; i < 2 && ret; i++)
			ret = FirmwareUpgrade(dev, fw);
	} else {
		release_firmware(fw);
		return -EPERM;
	}

	synaptics_reset_ctrl(dev, SW_RESET);
	release_firmware(fw);

	return 0;
}

static int synaptics_probe(struct device *dev)
{
	struct touch_core_data *ts = to_touch_core(dev);
	struct synaptics_data *d = NULL;

	TOUCH_TRACE();

	d = devm_kzalloc(dev, sizeof(*d), GFP_KERNEL);

	if (!d) {
		TOUCH_E("failed to allocate synaptics data\n");
		return -ENOMEM;
	}

	touch_set_device(ts, d);

	touch_gpio_init(ts->reset_pin, "touch_reset");
	touch_gpio_direction_output(ts->reset_pin, 0);

	touch_gpio_init(ts->int_pin, "touch_int");
	touch_gpio_direction_input(ts->int_pin);

	touch_power_init(dev);
	touch_bus_init(dev, 4096);

	synaptics_init_tci_info(dev);

	d->need_scan_pdt = true;
	d->ic_info.fw_pid_addr = 0x10;
	d->ic_info.fw_ver_addr = 0x1d100;

	return 0;
}

static struct touch_driver touch_driver = {
	.probe = synaptics_probe,
	.remove = synaptics_remove,
	.suspend = synaptics_suspend,
	.resume = synaptics_resume,
	.init = synaptics_init,
	.upgrade = synaptics_upgrade,
	.irq_handler = synaptics_irq_handler,
	.power = synaptics_power,
	.lpwg = synaptics_lpwg,
	.notify = synaptics_notify,
	.register_sysfs = synaptics_register_sysfs,
	.set = synaptics_set,
	.get = synaptics_get,
};


#define MATCH_NAME			"synaptics,TD4100"
/* #define MATCH_NAME                    "mediatek,cap_touch" */

static struct of_device_id touch_match_ids[] = {
	{.compatible = MATCH_NAME,},
};

static struct touch_hwif hwif = {
	.bus_type = HWIF_I2C,
	.name = LGE_TOUCH_NAME,
	.owner = THIS_MODULE,
	.of_match_table = of_match_ptr(touch_match_ids),
	/* .bits_per_word = 8, */
	/* .spi_mode = SPI_MODE_3, */
	/* .max_freq = (5 * 1000000), */

};

static int __init touch_device_init(void)
{
	TOUCH_TRACE();

	return touch_bus_device_init(&hwif, &touch_driver);
}

static void __exit touch_device_exit(void)
{
	TOUCH_TRACE();
	touch_bus_device_exit(&hwif);
}
module_init(touch_device_init);
module_exit(touch_device_exit);

MODULE_AUTHOR("hoyeon.jang@lge.com");
MODULE_DESCRIPTION("LGE touch driver v3");
MODULE_LICENSE("GPL");
