# Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 and
# only version 2 as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

import sys
import re
import os
import struct
import datetime
import array
import string
import bisect
import traceback
import binascii

from subprocess import *
from optparse import OptionParser
from optparse import OptionGroup
from struct import unpack
from ctypes import *
from tempfile import *
from print_out import *
from parser_util import register_parser, RamParser

SMEM_BASE_ADDR = 0x100000
NR_CPUS = 8
TASK_COMM_LEN = 16

class task_com(Structure):
    _fields_ = [
        ('task_name', TASK_COMM_LEN * c_char)
    ]

class last_reboot_reason_struct(Structure):
    _fields_ = [
        ('fiq_step', c_uint32),
	('exp_type', c_uint32),  # 0xaeedeadX: X=1 (HWT), X=2 (KE), X=3 (nested panic)
	('reboot_mode', c_uint32), 
	('last_irq_enter', NR_CPUS * c_uint32), 
	('jiffies_last_irq_enter', NR_CPUS * c_uint64),

        ('last_irq_exit', NR_CPUS * c_uint32),
	('jiffies_last_irq_exit', NR_CPUS * c_uint64),

	('jiffies_last_sched', NR_CPUS * c_uint64),
	('last_sched_comm', NR_CPUS * task_com),
	('hotplug_footprint', NR_CPUS * c_uint8),
	('hotplug_cpu_event', c_uint8),
	('hotplug_cb_index', c_uint8),
	('hotplug_cb_fp', c_uint64),

	('cpu_caller', c_uint32),
	('cpu_callee', c_uint32),
	('cpu_up_prepare_ktime', c_uint64),
	('cpu_starting_ktime', c_uint64),
	('cpu_online_ktime', c_uint64),
	('cpu_down_prepare_ktime', c_uint64),
	('cpu_dying_ktime', c_uint64),
	('cpu_dead_ktime', c_uint64),
	('cpu_post_dead_ktime', c_uint64),

	('mcdi_wfi', c_uint32),
	('mcdi_r15', c_uint32),
	('deepidle_data', c_uint32),
	('sodi3_data', c_uint32),
	('sodi_data', c_uint32),
	('spm_suspend_data', c_uint32),
	('spm_common_scenario_data', c_uint32),
	('cpu_dormant', NR_CPUS * c_uint64),
	('clk_data', 8 * c_uint32),
	('suspend_debug_flag', c_uint32),

	('vcore_dvfs_opp', c_uint32),
	('vcore_dvfs_status', c_uint32),

	('ppm_cluster_limit', 8 * c_uint32),
	('ppm_step', c_uint8),
	('ppm_cur_state', c_uint8),
	('ppm_min_pwr_bgt', c_uint32),
	('ppm_policy_mask', c_uint32),
	('ppm_waiting_for_pbm', c_uint32),

	('cpu_dvfs_vproc_big', c_uint8),
	('cpu_dvfs_vproc_little', c_uint8),
	('cpu_dvfs_oppidx', c_uint8),
	('cpu_dvfs_cci_oppidx', c_uint8),
	('cpu_dvfs_status', c_uint8),
	('cpu_dvfs_step', c_uint8),
	('cpu_dvfs_pbm_step', c_uint8),
	('cpu_dvfs_cb', c_uint8),
	('cpufreq_cb', c_uint8),

	('gpu_dvfs_vgpu', c_uint8),
	('gpu_dvfs_oppidx', c_uint8),
	('gpu_dvfs_status', c_uint8),

	('ptp_60', c_uint32),
	('ptp_64', c_uint32),
	('ptp_68', c_uint32),
	('ptp_6C', c_uint32),
	('ptp_78', c_uint32),
	('ptp_7C', c_uint32),
	('ptp_80', c_uint32),
	('ptp_84', c_uint32),
	('ptp_88', c_uint32),
	('ptp_8C', c_uint32),
	('ptp_9C', c_uint32),
	('ptp_A0', c_uint32),
	('ptp_vboot', c_uint64),
	('ptp_cpu_big_volt', c_uint64),
	('ptp_cpu_big_volt_1', c_uint64),
	('ptp_cpu_big_volt_2', c_uint64),
	('ptp_cpu_big_volt_3', c_uint64),
	('ptp_cpu_2_little_volt', c_uint64),
	('ptp_cpu_2_little_volt_1', c_uint64),
	('ptp_cpu_2_little_volt_2', c_uint64),
	('ptp_cpu_2_little_volt_3', c_uint64),
	('ptp_cpu_little_volt', c_uint64),
	('ptp_cpu_little_volt_1', c_uint64),
	('ptp_cpu_little_volt_2', c_uint64),
	('ptp_cpu_little_volt_3', c_uint64),
	('ptp_cpu_cci_volt', c_uint64),
	('ptp_cpu_cci_volt_1', c_uint64),
	('ptp_cpu_cci_volt_2', c_uint64),
	('ptp_cpu_cci_volt_3', c_uint64),
	('ptp_gpu_volt', c_uint64),
	('ptp_gpu_volt', c_uint64),
	('ptp_gpu_volt_2', c_uint64),
	('ptp_gpu_volt_3', c_uint64),
	('ptp_temp', c_uint64),
	('ptp_status', c_uint8),
	('eem_pi_offset', c_uint8),


	('thermal_temp1', c_uint8),
	('thermal_temp2', c_uint8),
	('thermal_temp3', c_uint8),
	('thermal_temp4', c_uint8),
	('thermal_temp5', c_uint8),
	('thermal_status', c_uint8),
	('thermal_ATM_status', c_uint8),
	('thermal_ktime', c_uint64),

	('isr_el1', c_uint8),

	('idvfs_ctrl_reg', c_uint32),
	('idvfs_enable_cnt', c_uint32),
	('idvfs_swreq_cnt', c_uint32),
	('idvfs_curr_volt', c_uint16),
	('idvfs_swavg_curr_pct_x100', c_uint16),
	('idvfs_swreq_curr_pct_x100', c_uint16),
	('idvfs_swreq_next_pct_x100', c_uint16),
	('idvfs_sram_ldo', c_uint16),
	('idvfs_state_manchine', c_uint8),

	('ocp_2_target_limit', c_uint32),
	('ocp_2_enable', c_uint8),
	('scp_pc', c_uint32),
	('scp_lr', c_uint32),

	('kparams_addr', c_uint32),
    ]


class ram_console_buffer_struct(Structure):
    _fields_ = [
        ('sig', c_uint32),
        ('off_pl', c_uint32),
        ('off_lpl', c_uint32),
        ('sz_pl', c_uint32),
        ('off_lk', c_uint32),
        ('off_llk', c_uint32),
        ('sz_lk', c_uint32),
        ('padding', 3 * c_uint32),
        ('sz_buffer', c_uint32),
        ('off_linux', c_uint32),
        ('off_console', c_uint32),
        ('log_start', c_uint32),
        ('log_size', c_uint32),
        ('sz_console', c_uint32),
    ]

@register_parser("--ram-console", "Print ram console status(if enabled)")
class ram_console(RamParser) :
    def print_out_str_console(self, out_file, in_string) :
        if out_file is None :
            print (in_string)
        else :
            out_file.write((in_string + '\n').encode('ascii', 'ignore'))
        return

    def parse_last_reboot_reason(self, rcb_phys, ShMemDumpFile, ram_console_buffer) :
        temp_array = []

        ShMemDumpFile.seek(rcb_phys - SMEM_BASE_ADDR + ram_console_buffer.off_linux, 0)
        last_reboot_reason = last_reboot_reason_struct()
        ShMemDumpFile.readinto(last_reboot_reason)

        RCBOutPutFile = self.ramdump.open_file("ram_console.txt")

        self.print_out_str_console(RCBOutPutFile, "===================================================== ")
        self.print_out_str_console(RCBOutPutFile, "Extracted Ram Console INFO ")
        self.print_out_str_console(RCBOutPutFile, "===================================================== \n")

        for field_name, field_type in last_reboot_reason._fields_ :
            if field_name in ['last_irq_enter', 'jiffies_last_irq_enter', 'last_irq_exit', 'jiffies_last_irq_exit', 'jiffies_last_sched', 'hotplug_footprint', 'cpu_dormant'] :
                for cpu in range(0, NR_CPUS):
                    code = compile('temp_array.append(last_reboot_reason.{0}[cpu])'.format(field_name), '<string>', 'single')
                    exec(code)
                self.print_out_str_console(RCBOutPutFile, '{0:30s} {1}'.format(field_name, temp_array))
            elif field_name in ['last_sched_comm']:
                for cpu in range(0, NR_CPUS):
                    temp_array.append(last_reboot_reason.last_sched_comm[cpu].task_name)
                self.print_out_str_console(RCBOutPutFile, '{0:30s} {1}'.format(field_name, temp_array))
            elif field_name in ['clk_data']:
                for cpu in range(0, 8):
                    code = compile('temp_array.append(last_reboot_reason.{0}[cpu])'.format(field_name), '<string>', 'single')
                    exec(code)
                self.print_out_str_console(RCBOutPutFile, '{0:30s} {1}'.format(field_name, temp_array))
            elif field_name in ['exp_type'] :
                self.print_out_str_console(RCBOutPutFile, '{0:30s} {1:x}'.format(field_name, getattr(last_reboot_reason, field_name)))
            else :
                self.print_out_str_console(RCBOutPutFile, '{0:30s} {1}'.format(field_name, getattr(last_reboot_reason, field_name)))
            del temp_array[:]

        self.print_out_str_console(RCBOutPutFile, "======== End Ram Console INFO ======================= \n")

    def extract_ram_console(self, rcb_phys, ShMemDumpFile, ram_console_buffer) :
        self.parse_last_reboot_reason(rcb_phys, ShMemDumpFile, ram_console_buffer)

    def ramconsole_Parser(self, ramdump, file_path) :
        try:
            ShMemDumpFile = open("{0}/smem.bin".format(file_path), "rb")
        except IOError, e:
            print '\n%s' % (e)
            sys.exit(1)

        RCB = ramdump.addr_lookup("ram_console_buffer")
        if RCB is None :
            print_out_str ("[!] RCB was not enabled in this build.")
            return
        rcb_phys = ramdump.virt_to_phys(ramdump.read_word(RCB))

        ShMemDumpFile.seek(rcb_phys - SMEM_BASE_ADDR, 0)
        ram_console_buffer = ram_console_buffer_struct()
        ShMemDumpFile.readinto(ram_console_buffer)

        if ram_console_buffer.sig != 0x43474244:
            print "ram_console magic is not valid"

        self.extract_ram_console(rcb_phys, ShMemDumpFile, ram_console_buffer)

    def __init__(self, *args):
        super(ram_console, self).__init__(*args)
        self.name_lookup_table = []
        
    def parse(self) :
        self.ramconsole_Parser(self.ramdump, self.ramdump.ebi_path)
