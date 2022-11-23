#ifndef __RT4832_H__
#define __RT4832_H__

void rt4832_dsv_ctrl(int enable);
void rt4832_dsv_toggle_ctrl(void);
#if defined(CONFIG_LGD_INCELL_LG4894_HD_LV5)
int rt4832_chargepump_set_backlight_level(unsigned int level);
#endif
#if defined(CONFIG_LGD_INCELL_LG4894_HD_LV5) || defined(CONFIG_LGD_INCELL_TD4100_HD_SF3)
extern int old_bl_level;
#endif
#endif
