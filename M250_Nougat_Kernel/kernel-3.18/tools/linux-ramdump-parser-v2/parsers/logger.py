# Copyright (c) 2012, Code Aurora Forum. All rights reserved.
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
import time
from subprocess import *
from optparse import OptionParser
from optparse import OptionGroup
from struct import unpack
from ctypes import *
from tempfile import *
from print_out import *
from parser_util import register_parser, RamParser

@register_parser("--logger", "print android logger (LGE)")
class logger(RamParser):
	out_file = None

	def print_out_str_logger(self, string) :
		if self.out_file is None :
			print_out_str(string)
		else :
			self.out_file.write((string+"\n").encode('ascii','ignore'))

	def print_out_str_logger_uni(self, string) :
		if self.out_file is None :
			print_out_str_uni(string)
		else :
			self.out_file.write(string+"\n")

	def filter_pri_to_char(self, pri) :
		return {
			b'\x02' : 'V',
			b'\x03' : 'D',
			b'\x04' : 'I',
			b'\x05' : 'W',
			b'\x06' : 'E',
			b'\x07' : 'F',
			b'\x08' : 'S',
			}.get(pri, '?')

	tagmap = {}

	def get4le(self, x) :
		return ord(x[0]) + ord(x[1]) * 0x100 + ord(x[2]) * 0x10000 + ord(x[3]) * 0x1000000

	def log_print_binary(self, buf, len) :
		try:
			logtype = ord(buf[0])
		except IndexError:
			print (" log_print_binary aborted..!!! ")
			return
		tmpbuf = buf[1:len]

		if logtype == 0 :
			eventdata = self.get4le(tmpbuf[0:4])
			result = (5, str(eventdata))
		elif logtype == 1 :
			eventdata = self.get4le(tmpbuf[0:4]) + self.get4le(tmpbuf[4:8]) * 0x100000000
			result = (9, str(eventdata))
		elif logtype == 2 :
			strlen = self.get4le(tmpbuf[0:4])
			result = (5 + strlen, tmpbuf[4:4+strlen])
		elif logtype == 3 :
			count = ord(tmpbuf[0])
			tmpbuf = tmpbuf[1:len]
			msg = '['
			size = 2
			i = 0
			while i < count :
				tmp = self.log_print_binary(tmpbuf, len-size)
				if tmp is None:
					return
				size += tmp[0]
				msg += tmp[1]
				tmpbuf = tmpbuf[tmp[0]:len]
				if i != count - 1 :
					msg += ','
				else :
					msg += ']'
				i += 1
			result = (size, msg)
		else :
			return
		return result

	def _print_logger(self, ram_dump, logger_addr) :
		misc_offset = ram_dump.field_offset("struct logger_log", "misc")
		buffer_offset = ram_dump.field_offset("struct logger_log", "buffer")
		woff_offset = ram_dump.field_offset("struct logger_log", "w_off")
		head_offset = ram_dump.field_offset("struct logger_log", "head")
		size_offset = ram_dump.field_offset("struct logger_log", "size")

		len_offset = ram_dump.field_offset("struct logger_entry", "len")
		pid_offset = ram_dump.field_offset("struct logger_entry", "pid")
		sec_offset = ram_dump.field_offset("struct logger_entry", "sec")
		nsec_offset = ram_dump.field_offset("struct logger_entry", "nsec")
		msg_offset = ram_dump.field_offset("struct logger_entry", "msg")
		entry_size = ram_dump.sizeof("struct logger_entry")

                miscname_offset = ram_dump.field_offset("struct miscdevice", "name")
		miscname = ram_dump.read_word(logger_addr + misc_offset + miscname_offset)
		log_name = ram_dump.read_cstring(miscname, 20)

		if log_name == "log_events" :
			binary_log = 1
		else :
			binary_log = 0

		self.print_out_str_logger ("\n=============================== {0:10}: start ===============================".format(log_name))

		buffer_addr = ram_dump.read_word(logger_addr + buffer_offset)
		w_off = ram_dump.read_word(logger_addr + woff_offset)
		head = ram_dump.read_word(logger_addr + head_offset)
		size = ram_dump.read_word(logger_addr + size_offset)

		if buffer_addr is None :
			self.print_out_str_logger ("buffer is null")
			return

		if head != 0 and head < w_off :
			self.print_out_str_logger ("head pointer is not correct")
			return

		i = head
		while (i != w_off) :
			n = 0

			len = ram_dump.read_u16(buffer_addr + i + len_offset)
			pid = ram_dump.read_u32(buffer_addr + i + pid_offset)
			sec = ram_dump.read_u32(buffer_addr + i + sec_offset)
			nsec = ram_dump.read_u32(buffer_addr + i + nsec_offset)
			buf = ram_dump.read_physical(ram_dump.virt_to_phys(buffer_addr + i + entry_size), len)

			ptm = time.localtime(sec)
			timestr = time.strftime("%m-%d %H:%M:%S", ptm)
			nsec_str = str(nsec)[0:3]

			if binary_log == 1 :
				eventdata = self.get4le(buf[0:4])
				try :
					tag = self.tagmap[eventdata]
				except :
					tag = '[' + str(eventdata) + ']'
				pri_chr = 'I'
				tmp = self.log_print_binary(buf[4:len], len-4)
				if tmp is None :
					break
				msg = tmp[1]
			else :
				try :
					while (buf[n] != b'\x00') :
						n = n + 1
				except IndexError:
					self.print_out_str_logger_uni ("%s - Parsing Error !!!!!!!!!!!!!!!!!!!!!" % log_name )
					break
				pri_chr = self.filter_pri_to_char(buf[0])
				tag = buf[1:(n-1) + 1]
				msg = buf[(n+1):(len-2) + 1]

				if msg[-1:] == b'\x0a' :
					msg = msg[:-1]

			self.print_out_str_logger_uni ("{0}.{1:3} {2}/{3:8}({4:5}): {5}".format(timestr, nsec_str, pri_chr, tag, pid, msg))

			i = i + entry_size + len

			if i > size :
				i = i - size;

		self.print_out_str_logger ("=============================== {0:10}: end ===============================".format(log_name))

	def print_logger(self, ram_dump, out_dir) :

		path = os.path.dirname(str(sys.argv[0]))
		if path == "" :
			path = "."
		f = open(path + "/parsers/event-log-tags", 'r')
		for line in f :
			s = line.split()
			self.tagmap[int(s[0], 10)] = s[1]

		ver = ram_dump.version
		if re.search('3.7.\d',ver) is None :
			list_offset = ram_dump.field_offset("struct logger_log", "logs")
			log_list_addr = ram_dump.addr_lookup("log_list")
			log_addr = ram_dump.read_word(log_list_addr)
			while (log_addr != log_list_addr) :
				self._print_logger(ram_dump, log_addr - list_offset)
				log_addr = ram_dump.read_word(log_addr)
		else :
			for log in ["log_main", "log_events", "log_system", "log_radio"] :
				logger_addr = ram_dump.addr_lookup(log)
				self.out_file = open("{0}/".format(out_dir)+log+".log", "wb")
				self._print_logger(ram_dump, logger_addr)
				self.out_file.close()
				self.out_file = None

	def parse(self) :
		self.print_logger(self.ramdump, self.ramdump.outdir)
