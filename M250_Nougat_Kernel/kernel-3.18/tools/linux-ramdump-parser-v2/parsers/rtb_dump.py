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
from subprocess import *
from optparse import OptionParser
from optparse import OptionGroup
from struct import unpack
from ctypes import *
from tempfile import *
from print_out import *
from parser_util import register_parser, RamParser

@register_parser("--print-rtb-dump", "Print RTB dump(if enabled)", shortopt="-m", optional=True)
class RTB_DUMP(RamParser) :
    def __init__(self, *args):
        super(RTB_DUMP, self).__init__(*args)
        self.name_lookup_table = []

    def parse(self) :
        rtb = self.ramdump.addr_lookup("mtk_rtb")
        out_dir = self.ramdump.outdir
        if rtb is None :
            print_out_str ("[!] RTB was not enabled in this build. No RTB files will be generated")
            return

        step_size_offset = self.ramdump.field_offset("struct mtk_rtb_state", "step_size")
        nentries_offset = self.ramdump.field_offset("struct mtk_rtb_state","nentries")
        rtb_entry_offset = self.ramdump.field_offset("struct mtk_rtb_state", "rtb")
        rtb_entry_phys = self.ramdump.field_offset("struct mtk_rtb_state", "phys")
        rtb_size_offset = self.ramdump.field_offset("struct mtk_rtb_state", "size")
        step_size = self.ramdump.read_u32(rtb + step_size_offset)
        total_entries = self.ramdump.read_int(rtb + nentries_offset)
        rtb_read_ptr = self.ramdump.read_word(rtb + rtb_entry_offset)
        rtb_read_phys = self.ramdump.read_word(rtb + rtb_entry_phys)
        mask = self.ramdump.read_word(rtb + nentries_offset) - 1

        size = self.ramdump.read_u32(rtb + rtb_size_offset)

        rtb_entry_size = self.ramdump.sizeof("struct mtk_rtb_layout")

        rtb_out = open("{0}/mtk_rtb_dump.txt".format(out_dir),"wb")
        rtb_out.write("******** RTB DUMP ********* base : {0:x}\n".format(rtb_read_phys))

        for i in range(0, total_entries * rtb_entry_size, rtb_entry_size) :
            rtb_line = rtb_read_ptr + i
            rtb_phys = rtb_read_phys + i
            rtb_out.write("{0:x}: ".format(rtb_phys))

            for j in range(0, rtb_entry_size, 4) :
                print_line = self.ramdump.read_u32(rtb_line + j)
                rtb_out.write("{0:0>8x} ".format(print_line))

            sentinel_offset = self.ramdump.field_offset("struct mtk_rtb_layout", "sentinel")
            sentinel = self.ramdump.read_u32(rtb_line + sentinel_offset) & 0x00ffffff
            idx_offset = self.ramdump.field_offset("struct mtk_rtb_layout", "idx")
            idx = self.ramdump.read_u32(rtb_line + idx_offset)
            pre_idx = self.ramdump.read_u32(rtb_line + idx_offset - (step_size * rtb_entry_size))

            cpu_core = (i % (step_size * rtb_entry_size)) / rtb_entry_size

            if (sentinel == 0) :
                rtb_out.write("\n")
                continue

            if (sentinel != 0xffaaff) :
                print_out_str("0x{0:x} : 0x{1:x} <= should be 0x00ffaaff Sentinel ERR    at CPU{2}".format(rtb_phys, sentinel, cpu_core))

            if (i < rtb_entry_size * step_size) :
                rtb_out.write("\n")
                continue

            if (idx != pre_idx + step_size) :
                if (idx == pre_idx - (size / rtb_entry_size) + step_size) :
                    rtb_out.write("\n")
                    continue
                print_out_str("0x{0:0>8x} : 0x{1:0>8x} <= should be 0x{2:0>8x} StepStamp ERR   at CPU{3}".format(rtb_phys, idx, pre_idx + step_size, cpu_core))
            rtb_out.write("\n")
        rtb_out.close()
