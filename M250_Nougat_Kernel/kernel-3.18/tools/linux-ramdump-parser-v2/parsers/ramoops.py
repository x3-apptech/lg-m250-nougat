#
# Copyright (c) 2014, LG Electronics. All rights reserved.
#
# ramoops parser for msm8992
#
# MSM8992_IMEM_BASE_ADDRESS 0xFE80F000
# RAM_CONSOLE_ADDR_ADDR     0x2C
# RAM_CONSOLE_SIZE_ADDR     0x30
#

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

# CONSOLE_ADDR_OFFSET = 0x0040
# CONSOLE_SIZE_OFFSET = 0x0044

CONSOLE_ADDR_BASE = 0x44410000
CONSOLE_SIZE = 0xE0000

def do_print_crash_log(ram_dump) :
    print_out_str ("\n===== crash handler log =====")
    # console_addr = ram_dump.read_u32(ram_dump.tz_start + CONSOLE_ADDR_OFFSET, False)
    # size = ram_dump.read_u32(ram_dump.tz_start + CONSOLE_SIZE_OFFSET, False)
    console_addr = CONSOLE_ADDR_BASE
    size = CONSOLE_SIZE

    log_buf_start = console_addr + size
    log_buf_end = console_addr + 0x100000

    buf = ram_dump.read_physical(log_buf_start, log_buf_end - log_buf_start)
    try :
        for f in buf.split('\n') :
            print_out_str(f)
    except :
        print_out_str ("===== end crash handler log =====\n")

def do_print_console(ram_dump) :
    print_out_str ("\n===== ramoops console =====")
    # console_addr = ram_dump.read_u32(ram_dump.tz_start + CONSOLE_ADDR_OFFSET, False)
    cprz_offset = ram_dump.field_offset("struct ramoops_context", "cprz")
    console_log_offset = ram_dump.field_offset("struct persistent_ram_zone", "paddr")
    console_size_offset = ram_dump.field_offset("struct persistent_ram_zone", "size")
    console_zone_addr = ram_dump.read_word(
            ram_dump.addr_lookup("oops_cxt") + cprz_offset)

    console_log_addr = ram_dump.read_word(console_zone_addr + console_log_offset)

    start = ram_dump.read_u32(console_log_addr + 0x4, False)
    size = ram_dump.read_u32(console_log_addr + 0x8, False)

    if ram_dump.read_u32(console_log_addr, False) != 0x43474244 :
        print_out_str("ram console signature is not correct")
        return

    if size > start :
        buf1 = ram_dump.read_physical(console_log_addr + 0xC + start, size - start)
        try :
            for f in buf1.split('\n') :
                print_out_str(f)
        except :
            print_out_str ("extraction error was occured!!")

    if start > 0 :
        buf2 = ram_dump.read_physical(console_log_addr + 0xC, start)
        try :
            for f in buf2.split('\n') :
                print_out_str(f)
        except :
            print_out_str ("extraction error was occured!!")
    print_out_str ("===== end ramoops console =====\n")

def do_print_console_mprz(ram_dump) :
    print_out_str ("\n===== ramoops mprz console =====")
    # console_addr = ram_dump.read_u32(ram_dump.tz_start + CONSOLE_ADDR_OFFSET, False)
    mprz_offset = ram_dump.field_offset("struct ramoops_context", "mprz")
    console_log_offset = ram_dump.field_offset("struct persistent_ram_zone", "paddr")
    console_size_offset = ram_dump.field_offset("struct persistent_ram_zone", "size")
    console_zone_addr = ram_dump.read_word(
            ram_dump.addr_lookup("oops_cxt") + mprz_offset)

    console_log_addr = ram_dump.read_word(console_zone_addr + console_log_offset)

    start = ram_dump.read_u32(console_log_addr + 0x4, False)
    size = ram_dump.read_u32(console_log_addr + 0x8, False)

    if ram_dump.read_u32(console_log_addr, False) != 0x43474244 :
        print_out_str("ram console mprz signature is not correct")
        return

    if size > start :
        buf1 = ram_dump.read_physical(console_log_addr + 0xC + start, size - start)
        try :
            for f in buf1.split('\n') :
                print_out_str(f)
        except :
            print_out_str ("extraction error was occured!!")

    if start > 0 :
        buf2 = ram_dump.read_physical(console_log_addr + 0xC, start)
        try :
            for f in buf2.split('\n') :
                print_out_str(f)
        except :
            print_out_str ("extraction error was occured!!")
    print_out_str ("===== end ramoops mprz console =====\n")

def do_print_console_bprz(ram_dump) :
    print_out_str ("\n===== ramoops bprz console =====")
    # console_addr = ram_dump.read_u32(ram_dump.tz_start + CONSOLE_ADDR_OFFSET, False)
    bprz_offset = ram_dump.field_offset("struct ramoops_context", "bprz")
    console_log_offset = ram_dump.field_offset("struct persistent_ram_zone", "paddr")
    console_size_offset = ram_dump.field_offset("struct persistent_ram_zone", "size")
    console_zone_addr = ram_dump.read_word(
            ram_dump.addr_lookup("oops_cxt") + bprz_offset)

    console_log_addr = ram_dump.read_word(console_zone_addr + console_log_offset)

    start = ram_dump.read_u32(console_log_addr + 0x4, False)
    size = ram_dump.read_u32(console_log_addr + 0x8, False)

    if ram_dump.read_u32(console_log_addr, False) != 0x43474244 :
        print_out_str("ram console bprz signature is not correct")
        return

    if size > start :
        buf1 = ram_dump.read_physical(console_log_addr + 0xC + start, size - start)
        try :
            for f in buf1.split('\n') :
                print_out_str(f)
        except :
            print_out_str ("extraction error was occured!!")

    if start > 0 :
        buf2 = ram_dump.read_physical(console_log_addr + 0xC, start)
        try :
            for f in buf2.split('\n') :
                print_out_str(f)
        except :
            print_out_str ("extraction error was occured!!")
    print_out_str ("===== end ramoops bprz console =====\n")


def do_print_old_console(ram_dump) :
    print_out_str ("\n===== last console =====")
    cprz_offset = ram_dump.field_offset("struct ramoops_context", "cprz")
    old_log_offset = ram_dump.field_offset("struct persistent_ram_zone", "old_log")
    old_log_size_offset = ram_dump.field_offset("struct persistent_ram_zone", "old_log_size")
    console_zone_addr = ram_dump.read_word(
            ram_dump.addr_lookup("oops_cxt") + cprz_offset)

    old_log = ram_dump.read_word(console_zone_addr + old_log_offset)
    old_log_size = ram_dump.read_word(console_zone_addr + old_log_size_offset)

    if old_log != 0 :
        buf = ram_dump.read_physical(ram_dump.virt_to_phys(old_log), old_log_size)
        try :
            for f in buf.split('\n') :
                print_out_str(f)
        except :
            print_out_str ("extraction error was occured!!")
    else :
        print_out_str ("old_log is null. there is no last console!!")

    print_out_str ("===== end last console =====")

@register_parser("--print-ramoops", "print ramoops console, last console log")
class Ramoops(RamParser) :
    def parse(self) :
        do_print_crash_log(self.ramdump)
        do_print_console(self.ramdump)
        do_print_console_bprz(self.ramdump)
        do_print_console_mprz(self.ramdump)
        do_print_old_console(self.ramdump)
