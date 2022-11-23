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

class PMU_ChargerStruct(Structure):
    _fields_ = [
        ('bat_exist', c_uint32),
        ('bat_full', c_uint32),
        ('bat_charging_state', c_int),
        ('bat_vol', c_uint),
        ('bat_in_recharging_state', c_uint32),
        ('Vsense', c_uint),
        ('charger_exist', c_uint32),
        ('charger_vol', c_uint),
        ('charger_protect_status', c_int),
        ('ICharging', c_int),
        ('IBattery', c_int),
        ('temperature', c_int),
        ('temperatureR', c_int),
        ('temperatureV', c_int),
        ('total_charging_time', c_uint),
        ('PRE_charging_time', c_uint),
        ('CC_charging_time', c_uint),
        ('TOPOFF_charging_time', c_uint),
        ('POSTFULL_charging_time', c_uint),
        ('charger_type', c_uint),
        ('SOC', c_int),
        ('UI_SOC', c_int),
        ('UI_SOC2', c_int),
        ('nPercent_ZCV', c_uint),
        ('nPrecent_UI_SOC_check_point', c_uint),
        ('ZCV', c_uint),
        ('pseudo_batt_enabled', c_uint32),
        ('usb_current_max_enabled', c_uint32),
        ('store_demo_enabled', c_int),
        ('vzw_chg_mode', c_int),
        ('vzw_under_current_count', c_int),
        ('is_usb', c_int),
    ]

@register_parser("--bmt-status", "Print BMT status(if enabled)")
class BMT_STATUS(RamParser) :
    def deserialize(self, ctypesObj, inputBytes):
        fit = min(len(inputBytes), sizeof(ctypesObj))
        memmove(addressof(ctypesObj), inputBytes, fit)
        return ctypesObj

    def __init__(self, *args):
        super(BMT_STATUS, self).__init__(*args)
        self.name_lookup_table = []

    def parse(self) :
        bmt = self.ramdump.addr_lookup("BMT_status")
        BMT_status = PMU_ChargerStruct()
        if bmt is None :
            print_out_str ("[!] BMT was not enabled in this build. No BMT files will be generated")
            return
        bmt_phys = self.ramdump.virt_to_phys(bmt)

        PMU = self.ramdump.read_physical(bmt_phys, self.ramdump.sizeof('PMU_ChargerStruct'), 0)
        if (PMU is None) or (PMU == ''):
            print 'address {0:%x}' % (bmt_phys)

        bmt_info = self.deserialize(BMT_status, PMU)

        for field_name, field_type in bmt_info._fields_:
            print_out_str('{0:s}   {1:d}'.format(field_name, getattr(bmt_info, field_name)))
