import StringIO
from ctypes import *
from print_out import *
from parser_util import cleanupString, register_parser, RamParser

TZ_HWUID_NUM = 4
TZ_HRID_NUM = 2
TZ_DEVINFO_NUM = 4
DDR_BASE_ADDR = 0x40000000
SMEM_BASE_ADDR = 0x100000
DDR_ATF_LOG_BACKUP = 0x46460000

class crash_footprint_t(Structure):
  _fields_ = [
    ('magic_key', c_uint32),
    ('reboot_reason', c_uint32),
    ('fault_cpu', c_uint32),
    ('regs', c_uint32 * 21),
    ('modem_info', c_uint32),
    ('modem_will', c_uint8 * 256),
    ('ramconsole_paddr', c_uint32),
    ('ramconsole_size', c_uint32),
    ('fb_addr', c_uint32),
    ('atf_buf_addr', c_uint32),
    ('atf_buf_size', c_uint32),
  ]

@register_parser("--tz-log", "print tzdump information")
class tzdump_mtk(RamParser):
  #========================================================
  # Function Definition's
  #========================================================
  # String printing into out file
  def print_out_str_tz(self, out_file, in_string) :
    if out_file is None :
      print (in_string)
    else :
      out_file.write((in_string + '\n').encode('ascii', 'ignore'))
    return

  def TzDumpParser(self, ramdump, file_path) :
  # Open the Shared Mem file to determine the location of the TZ dump, and prepare
  # the dump file for read as well

    SMEMDumpFile = open("{0}/smem.bin".format(file_path), "rb")
    LogDumpFile = open("{0}/ramdump.bin".format(file_path), "rb")
    TzOutPutFile = self.ramdump.open_file("tzdump.txt")

    crash_footprint_addr = self.ramdump.addr_lookup('crash_footprint')
    crash_p_footprint = self.ramdump.virt_to_phys(self.ramdump.read_word(crash_footprint_addr))

    SMEMDumpFile.seek(crash_p_footprint - SMEM_BASE_ADDR, 0)
    crash_footprint = crash_footprint_t()
    SMEMDumpFile.readinto(crash_footprint)  # reading offset variables from dump

    self.print_out_str_tz(TzOutPutFile, "===================================================== ")
    self.print_out_str_tz(TzOutPutFile, "Extracted TZBSP Log ")
    self.print_out_str_tz(TzOutPutFile, "===================================================== \n")

    LogDumpFile.seek(DDR_ATF_LOG_BACKUP - DDR_BASE_ADDR, 0)
    log_ringbuf = (LogDumpFile.read(crash_footprint.atf_buf_size))

    TzOutPutFile.write(cleanupString(log_ringbuf.decode('ascii', 'ignore').replace('\r','')) + '\n')

    self.print_out_str_tz(TzOutPutFile, "===== end tz log =====\n")

    # Close files and return
    SMEMDumpFile.close()
    LogDumpFile.close()
    TzOutPutFile.close()

  def parse(self) :
    self.TzDumpParser(self.ramdump, self.ramdump.ebi_path)
    print_out_str("Parsing result of tzdump was saved to {0}/tzdump.txt".format(self.ramdump.outdir))
