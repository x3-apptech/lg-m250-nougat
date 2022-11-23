/*
* Copyright (C) 2016 MediaTek Inc.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See http://www.gnu.org/licenses/gpl-2.0.html for more details.
*/
#ifndef _DISP_RECOVERY_H_
#define _DISP_RECOVERY_H_

#define GPIO_EINT_MODE	0
#define GPIO_DSI_MODE	1



/* defined in mtkfb.c should move to mtkfb.h*/
extern unsigned int islcmconnected;
extern unsigned int mmsys_enable;

void primary_display_check_recovery_init(void);
void primary_display_esd_check_enable(int enable);
unsigned int need_wait_esd_eof(void);

#if defined(CONFIG_LGD_INCELL_LG4894_HD_LV5)
void LG_ESD_recovery(void);
void mtkfb_esd_recovery(void);
bool _is_enable_esd_check(void);
void primary_esd_external_detected(void);
#elif defined(CONFIG_LGD_INCELL_TD4100_HD_SF3)
void mtkfb_esd_recovery(void);
void LG_ESD_recovery(void);
#elif defined(CONFIG_TOVIS_INCELL_TD4100_HD_LV7)
void mtkfb_esd_recovery(void);
void LG_ESD_recovery(void);
#endif

#endif
