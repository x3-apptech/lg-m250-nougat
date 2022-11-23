# This parser compare cache footprint with ramdump value.
# If there is a memroy corruption, indicate its information.
# If you find some bug, please contact to me.
# 2014/10/21 v0.1 Initial release by darkwood.kim

import re
import string
from print_out import *
from parser_util import register_parser, RamParser
import binascii

GCC_RESET_STATUS = [
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "TSENSE_RESET_STATUS",
    "SRST_STATUS",
    "WDOG_RESET_STATUS_1",
    "WDOG_RESET_STATUS_0"
]

PON_PON_REASON_1 = [
    "KPDPWR_N",
    "CBLPWR_N",
    "PON1",
    "USB_CHG",
    "DC_CHG",
    "RTC",
    "SMPL",
    "HARD_RESET"
]

PON_DUMMY = [
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A",
    "N/A"
]

PON_WARM_RESET_REASON1 = [
    "KPDPWR_N",
    "RESIN_N",
    "KPDPWR_AND_RESIN",
    "GP2",
    "GP1",
    "PMIC_WD",
    "PS_HOLD",
    "SOFT"
]

PON_WARM_RESET_REASON2 = [
    "N/A",
    "N/A",
    "N/A",
    "AFP",
    "N/A",
    "N/A",
    "N/A",
    "N/A"
]

PON_POFF_REASON1 = [
    "KPDPWR_N",
    "RESIN_N",
    "KPDPWR_AND_RESIN",
    "GP2",
    "GP1",
    "PMIC_WD",
    "PS_HOLD",
    "SOFT"
]

PON_POFF_REASON2 = [
    "STAGE3",
    "OTST3",
    "UVLO",
    "AFP",
    "CHARGER",
    "VDD_RB",
    "N/A",
    "N/A"
]

PON_SOFT_RESET_REASON1 = [
    "KPDPWR_N",
    "RESIN_N",
    "KPDPWR_AND_RESIN",
    "GP2",
    "GP1",
    "PMIC_WD",
    "PS_HOLD",
    "SOFT"
]

PON_SOFT_RESET_REASON2 = [
    "N/A",
    "N/A",
    "N/A",
    "AFP",
    "N/A",
    "N/A",
    "N/A",
    "N/A"
]

RST_STAT = [
    ["GCC_RESET_STATUS", GCC_RESET_STATUS]
]

PMIC_PON = [
    ["PON_PON_REASON_1", PON_PON_REASON_1],
    ["PON_DUMMY", PON_DUMMY],
    ["PON_WARM_RESET_REASON1", PON_WARM_RESET_REASON1],
    ["PON_WARM_RESET_REASON2", PON_WARM_RESET_REASON2],
    ["PON_POFF_REASON1", PON_POFF_REASON1],
    ["PON_POFF_REASON2", PON_POFF_REASON2],
    ["PON_SOFT_RESET_REASON1", PON_SOFT_RESET_REASON1],
    ["PON_SOFT_RESET_REASON2", PON_SOFT_RESET_REASON2]
]

REASON = [
    ["RST_STAT.BIN", RST_STAT],
    ["PMIC_PON.BIN", PMIC_PON]
]

@register_parser("--reset-reason", "Show reset reason.")
class ResetReason(RamParser):
    def __init__(self, *args):
        super(ResetReason, self).__init__(*args)

    def check_reason(self, reg_name, mask_name, value):
        if value != 0x0:
            print_out_str("Register name : " + reg_name)
        for i in range(7, -1, -1):
            if (value >> i) & 0x1:
                print_out_str("    " + mask_name[7-i])

    def do_reason_parse(self, file_name, data):
        f = open(file_name, "rb")

        for i in range(len(data)):
            self.check_reason(data[i][0], data[i][1], int(binascii.hexlify(f.read(1)), 16))
        f.close()

    def parse(self) :
        for i in range(len(REASON)):
            self.do_reason_parse(REASON[i][0], REASON[i][1])
