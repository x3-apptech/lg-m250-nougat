import sys
import os
import re
import struct
import string
from ctypes import *

class dcc_data(Structure):
    _pack_ = 1
    _fields_ = [
        ('signature', 8 * c_uint8),
        ('version', c_uint32),
        ('validity_flag', c_uint32),
        ('os_data', c_uint64),
        ('cpu_context', 8 * c_uint8),
        ('reset_trigger', c_uint32),
        ('dump_size', c_uint64),
        ('total_dump_size_required', c_uint64),
        ('sections_count', c_uint32),
    ]

class type_specific_info(Union):
    _pack_ = 1
    _fields_ = [
        ('base_addr', c_uint64),
        ('cpu_content', 6 * c_uint8),
        ('sv_specific', 16 * c_uint8),
    ]

class boot_raw_partition_dump_section_header(Structure):
    _pack_ = 1
    _fields_ = [
        ('validity_flag', c_uint32),
        ('section_version', c_uint32),
        ('section_type', c_uint32),
        ('section_offset', c_uint64),
        ('section_size', c_uint64),
        ('section_info', type_specific_info),
        ('section_name', 20 * c_uint8),
    ]

def parse_dump_info(file_path):
    dump_info = None
    rawdump_path = file_path + '/'+ 'rawdump.bin'
    dump_info_txt = file_path + '/' + 'dump_info.txt'
    dump_info = dict()
    if os.path.exists(dump_info_txt) :
        with open(dump_info_txt, 'rb') as fd:
            for line in fd.readlines() :
                match = re.search(r'\s+([0-9]+)\s+(0x\w+)\s+(\d+)\s+([a-zA-Z]+[\w\s]*[a-zA-Z]+   )\s+([\w.]+)', line)
                if match :
                    dump_info[match.group(5)] = match.group(2)
    elif os.path.exists(rawdump_path) :
        with open(rawdump_path, 'rb') as fd:
            rawdump_dcc_data = dcc_data()
            fd.readinto(rawdump_dcc_data)
            for index in range(0, rawdump_dcc_data.sections_count ,1):
                rawdump_section_header = boot_raw_partition_dump_section_header()
                fd.readinto(rawdump_section_header)
                c_s = ''.join(chr(int(v)) for v in list(rawdump_section_header.section_name))
                c_s = c_s.replace('\x00', '')
                section_base = hex(rawdump_section_header.section_info.base_addr)
                dump_info[c_s] = section_base.replace('L', '')
    else :
        print "No file to parse memory offset"
        return None
    return dump_info
