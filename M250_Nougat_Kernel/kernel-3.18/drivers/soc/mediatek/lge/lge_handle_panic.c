/*
 * arch/arm/mach-msm/lge/lge_handle_panic.c
 *
 * Copyright (C) 2010 LGE, Inc
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/kdebug.h>
#include <asm/setup.h>
#include <linux/module.h>

#ifdef CONFIG_CPU_CP15_MMU
#include <linux/ptrace.h>
#endif

#include <soc/mediatek/lge/board_lge.h>
#include <soc/mediatek/lge/lge_handle_panic.h>
#include <linux/input.h>

#ifdef CONFIG_OF_RESERVED_MEM
#include <linux/memblock.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_reserved_mem.h>
#endif

#define PANIC_HANDLER_NAME	"panic-handler"

unsigned int lge_crash_reason_magic;

extern phys_addr_t mtkfb_get_fb_base(void);

static DEFINE_SPINLOCK(lge_panic_lock);


struct _lge_crash_footprint *crash_footprint;
static int dummy_arg;

#ifdef CONFIG_LGE_HANDLE_PANIC_BY_KEY
static int key_crash_cnt = 0;
static int crash_combi[6] = {115, 114, 116, 115, 114, 115}; // 114: vol_down, 115: vol_up, 116: pwr

void lge_gen_key_panic(int key)
{
	if(crash_handle_get_status() != 1)
		return;

	if(key != crash_combi[key_crash_cnt++]){
		pr_info("Ready to panic: cleard\n");
		key_crash_cnt = 0;
	} else{
		pr_info("Ready to panic: count %d\n", key_crash_cnt);
	}

	if(key_crash_cnt == 6){
		panic("%s: Generate panic by key!\n", __func__);
	}
}
#endif

static int gen_bug(const char *val, struct kernel_param *kp)
{
	BUG();

	return 0;
}
module_param_call(gen_bug, gen_bug,
		param_get_bool, &dummy_arg, S_IWUSR | S_IRUGO);

static int gen_panic(const char *val, struct kernel_param *kp)
{
	panic("LGE Panic Handler Panic Test");

	return 0;
}
module_param_call(gen_panic, gen_panic,
		param_get_bool, &dummy_arg, S_IWUSR | S_IRUGO);

static int gen_null_pointer_exception(const char *val, struct kernel_param *kp)
{
	int *panic_test;

	panic_test = 0;
	*panic_test = 0xa11dead;

	return 0;
}
module_param_call(gen_null_pointer_exception, gen_null_pointer_exception,
		param_get_bool, &dummy_arg, S_IWUSR | S_IRUGO);

static u32 _lge_get_reboot_reason(void)
{
	return crash_footprint->reboot_reason;
}

u32 lge_get_reboot_reason(void)
{
	return _lge_get_reboot_reason();
}
EXPORT_SYMBOL(lge_get_reboot_reason);

static void _lge_set_reboot_reason(u32 rr)
{
	crash_footprint->reboot_reason = rr;

	return;
}

void lge_set_reboot_reason(u32 rr)
{
	_lge_set_reboot_reason(rr);

	return;
}
EXPORT_SYMBOL(lge_set_reboot_reason);

void lge_set_modem_info(u32 modem_info)
{
	crash_footprint->modem_info= modem_info;

	return;
}
EXPORT_SYMBOL(lge_set_modem_info);

void lge_set_modem_will(const char *modem_will)
{
	memset(crash_footprint->modem_will, 0x0, LGE_MODEM_WILL_SIZE);
	strncpy(crash_footprint->modem_will,
			modem_will, LGE_MODEM_WILL_SIZE - 1);

	return;
}
EXPORT_SYMBOL(lge_set_modem_will);
#ifdef CONFIG_OF_RESERVED_MEM

static void lge_panic_handler_free_page(unsigned long mem_addr, unsigned long size)
{
	unsigned long pfn_start, pfn_end, pfn_idx;

	pfn_start = mem_addr >> PAGE_SHIFT;
	pfn_end = (mem_addr + size) >> PAGE_SHIFT;
	for (pfn_idx = pfn_start; pfn_idx < pfn_end; pfn_idx++)
		free_reserved_page(pfn_to_page(pfn_idx));
}

static void lge_panic_handler_reserve_cleanup(unsigned long addr,unsigned long size)
{
	pr_info("reserved-memory free[@0x%lx+@0x%lx)\n",addr, size);
	if (addr == 0 || size == 0)
		return;
	memblock_free(addr, size);
	lge_panic_handler_free_page(addr, size);
}

static unsigned long preloader_mem_addr = 0;
static unsigned long preloader_mem_size = 0;
static unsigned long lk_mem_addr = 0;
static unsigned long lk_mem_size = 0;
static int get_preloader_reserve_mem(struct reserved_mem *rmem)
{
	preloader_mem_addr = rmem->base;
	preloader_mem_size = rmem->size;
	return 0;
}

static int get_lk_reserve_mem(struct reserved_mem *rmem)
{
	lk_mem_addr = rmem->base;
	lk_mem_size = rmem->size;
	return 0;
}

static void release_reserve_mem(void)
{
	lge_panic_handler_reserve_cleanup(preloader_mem_addr,preloader_mem_size);
	lge_panic_handler_reserve_cleanup(lk_mem_addr,lk_mem_size);

	preloader_mem_addr = 0;
	preloader_mem_size = 0;
	lk_mem_addr = 0;
	lk_mem_size = 0;
}

RESERVEDMEM_OF_DECLARE(lge_panic_handler_pre_reserved_memory, "mediatek,preloader", get_preloader_reserve_mem);
RESERVEDMEM_OF_DECLARE(lge_panic_handler_lk_reserved_memory, "mediatek,lk", get_lk_reserve_mem);
#endif

static int reserve_enable   = 1;
static int reserve_enable_set(const char *val, struct kernel_param *kp)
{
    int ret;

    ret = param_set_int(val, kp);
    if (ret)
        return ret;
    printk(KERN_INFO "[LGE] reserve_enable %d\n",reserve_enable);
    if (!reserve_enable)  {
#ifdef CONFIG_OF_RESERVED_MEM
		release_reserve_mem();
#endif
    }
    return  0;
}
module_param_call(reserve_enable, reserve_enable_set, param_get_int,
    &reserve_enable, S_IRUGO|S_IWUSR|S_IWGRP);
#ifdef CONFIG_MTK_CPU_HOTPLUG_DEBUG_3
extern unsigned int timestamp_enable;
#endif
static int crash_handle_status = 1;
static int crash_handle_set_status(const char *val, struct kernel_param *kp)
{
	int ret;
	unsigned long flags;

	ret = param_set_int(val, kp);
	if (ret) {
		return ret;
	}

	spin_lock_irqsave(&lge_panic_lock, flags);

	if (crash_handle_status) {
		lge_crash_reason_magic = 0x6D630000;
		_lge_set_reboot_reason(LGE_CRASH_UNKNOWN);
#ifdef CONFIG_MTK_CPU_HOTPLUG_DEBUG_3
		timestamp_enable = 1;
#endif
	} else {
		lge_crash_reason_magic = 0x636D0000;
		_lge_set_reboot_reason(LGE_CRASH_UNKNOWN);
#ifdef CONFIG_MTK_CPU_HOTPLUG_DEBUG_3
		timestamp_enable = 0;
#endif
	}

	spin_unlock_irqrestore(&lge_panic_lock, flags);

	return 0;
}
module_param_call(crash_handle_enable, crash_handle_set_status,
		param_get_int, &crash_handle_status,
		S_IRUGO | S_IWUSR | S_IRUGO);

int crash_handle_get_status(void)
{
	return crash_handle_status;
}
EXPORT_SYMBOL(crash_handle_get_status);

void lge_set_ram_console_addr(unsigned int addr, unsigned int size)
{
	crash_footprint->ramconsole_paddr = addr;
	crash_footprint->ramconsole_size = size;
}

static inline void lge_save_ctx(struct pt_regs *regs)
{
	unsigned int sctrl, ttbr0, ttbr1, ttbcr;
	struct pt_regs context;
	int id, current_cpu;

	asm volatile ("mrc p15, 0, %0, c0, c0, 5 @ Get CPUID\n":"=r"(id));
	current_cpu = (id & 0x3) + ((id & 0xF00) >> 6);

	crash_footprint->fault_cpu = current_cpu;

	if (regs == NULL) {

		asm volatile ("stmia %1, {r0 - r15}\n\t"
				"mrs %0, cpsr\n"
				:"=r" (context.uregs[16])
				: "r"(&context)
				: "memory");

		/* save cpu register for simulation */
		crash_footprint->regs[0] = context.ARM_r0;
		crash_footprint->regs[1] = context.ARM_r1;
		crash_footprint->regs[2] = context.ARM_r2;
		crash_footprint->regs[3] = context.ARM_r3;
		crash_footprint->regs[4] = context.ARM_r4;
		crash_footprint->regs[5] = context.ARM_r5;
		crash_footprint->regs[6] = context.ARM_r6;
		crash_footprint->regs[7] = context.ARM_r7;
		crash_footprint->regs[8] = context.ARM_r8;
		crash_footprint->regs[9] = context.ARM_r9;
		crash_footprint->regs[10] = context.ARM_r10;
		crash_footprint->regs[11] = context.ARM_fp;
		crash_footprint->regs[12] = context.ARM_ip;
		crash_footprint->regs[13] = context.ARM_sp;
		crash_footprint->regs[14] = context.ARM_lr;
		crash_footprint->regs[15] = context.ARM_pc;
		crash_footprint->regs[16] = context.ARM_cpsr;
	} else {
		memcpy(crash_footprint->regs, regs, sizeof(unsigned int) * 17);
	}


	/*
	 * SCTRL, TTBR0, TTBR1, TTBCR
	 */
	asm volatile ("mrc p15, 0, %0, c1, c0, 0\n" : "=r" (sctrl));
	asm volatile ("mrc p15, 0, %0, c2, c0, 0\n" : "=r" (ttbr0));
	asm volatile ("mrc p15, 0, %0, c2, c0, 1\n" : "=r" (ttbr1));
	asm volatile ("mrc p15, 0, %0, c2, c0, 2\n" : "=r" (ttbcr));

	printk(KERN_INFO "SCTRL: %08x  TTBR0: %08x\n", sctrl, ttbr0);
	printk(KERN_INFO "TTBR1: %08x  TTBCR: %08x\n", ttbr1, ttbcr);

	/* save mmu register for simulation */
	crash_footprint->regs[17] = sctrl;
	crash_footprint->regs[18] = ttbr0;
	crash_footprint->regs[19] = ttbr1;
	crash_footprint->regs[20] = ttbcr;

	return;
}

static int lge_handler_panic(struct notifier_block *this,
			unsigned long event,
			void *ptr)
{
	unsigned long flags;

	spin_lock_irqsave(&lge_panic_lock, flags);

	printk(KERN_CRIT "%s called\n", __func__);

	lge_save_ctx(NULL);

	if (_lge_get_reboot_reason() == LGE_CRASH_UNKNOWN) {
		_lge_set_reboot_reason(LGE_CRASH_KERNEL_PANIC);
	}

	spin_unlock_irqrestore(&lge_panic_lock, flags);

	return NOTIFY_DONE;
}

static int lge_handler_die(struct notifier_block *self,
			unsigned long cmd,
			void *ptr)
{
	struct die_args *dargs = (struct die_args *) ptr;
	unsigned long flags;

	spin_lock_irqsave(&lge_panic_lock, flags);

	printk(KERN_CRIT "%s called\n", __func__);

	lge_save_ctx(dargs->regs);

	/*
	 * reboot reason setting..
	 */
	if (_lge_get_reboot_reason() == LGE_CRASH_UNKNOWN) {
		_lge_set_reboot_reason(LGE_CRASH_KERNEL_OOPS);
	}

	spin_unlock_irqrestore(&lge_panic_lock, flags);

	return NOTIFY_DONE;
}

static int lge_handler_reboot(struct notifier_block *self,
			unsigned long cmd,
			void *ptr)
{
	unsigned long flags;

	spin_lock_irqsave(&lge_panic_lock, flags);

	printk(KERN_CRIT "%s called\n", __func__);

	if (crash_footprint->reboot_reason == LGE_CRASH_UNKNOWN) {
		crash_footprint->reboot_reason = LGE_REBOOT_REASON_NORMAL;
	}

	spin_unlock_irqrestore(&lge_panic_lock, flags);

	return NOTIFY_DONE;
}

static struct notifier_block lge_panic_blk = {
	.notifier_call  = lge_handler_panic,
	.priority	= 1004,
};

static struct notifier_block lge_die_blk = {
	.notifier_call	= lge_handler_die,
	.priority	= 1004,
};

static struct notifier_block lge_reboot_blk = {
	.notifier_call	= lge_handler_reboot,
	.priority	= 1004,
};

static int __init lge_panic_handler_early_init(void)
{
	size_t start;
	size_t size;
	void *buffer;

	start = LGE_BSP_RAM_CONSOLE_PHY_ADDR;
	size = LGE_BSP_RAM_CONSOLE_SIZE;

	printk(KERN_INFO "LG console start addr : 0x%x\n",
			(unsigned int) start);
	printk(KERN_INFO "LG console end addr : 0x%x\n",
			(unsigned int) (start + size));

	buffer = ioremap(start, size);
	if (buffer == NULL) {
		printk(KERN_ERR "lge_panic_handler: failed to map memory\n");
		return -ENOMEM;
	}

	lge_crash_reason_magic = 0x6D630000;

	memset(buffer, 0x0, size);
	crash_footprint = (struct _lge_crash_footprint *) buffer;
	crash_footprint->magic_key = LGE_CONSOLE_MAGIC_KEY;
	_lge_set_reboot_reason(LGE_CRASH_UNKNOWN);

	crash_footprint->fb_addr = (u32)mtkfb_get_fb_base();
	atomic_notifier_chain_register(&panic_notifier_list, &lge_panic_blk);
	register_die_notifier(&lge_die_blk);
	register_reboot_notifier(&lge_reboot_blk);

	return 0;
}
early_initcall(lge_panic_handler_early_init);

static int __init lge_panic_handler_probe(struct platform_device *pdev)
{
	int ret = 0;

	return ret;
}

static int lge_panic_handler_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver panic_handler_driver __refdata = {
	.probe	= lge_panic_handler_probe,
	.remove = lge_panic_handler_remove,
	.driver = {
		.name = PANIC_HANDLER_NAME,
		.owner = THIS_MODULE,
	},
};

static int __init lge_panic_handler_init(void)
{
	return platform_driver_register(&panic_handler_driver);
}

static void __exit lge_panic_handler_exit(void)
{
	platform_driver_unregister(&panic_handler_driver);
}

module_init(lge_panic_handler_init);
module_exit(lge_panic_handler_exit);

MODULE_DESCRIPTION("LGE panic handler driver");
MODULE_AUTHOR("SungEun Kim <cleaneye.kim@lge.com>");
MODULE_LICENSE("GPL");
