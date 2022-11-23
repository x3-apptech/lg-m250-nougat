#
# Network On Chip Parser
# ref: 80-NN899-22_A_MSM8992_NoC_Error_Debug
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
import StringIO
from subprocess import *
from optparse import OptionParser
from optparse import OptionGroup
from struct import unpack
from ctypes import *
from print_out import *
from parser_util import register_parser, RamParser

BID = [
    "PNoC",
    "SNoC",
    "BIMC",
    "MNoC",
    "CNoC"
    "",
    "",
    "",
]

SNoC_InitFlow = [
    "Flow:/qhm_qdss_bam/I/O",
    "Flow:/qhm_snoc_cfg/I/O",
    "Flow:/qxm_bimc/I/O",
    "Flow:/qxm_cnoc/I/O",
    "Flow:/qxm_crypto0/I/O",
    "Flow:/qxm_crypto1/I/O",
    "Flow:/qxm_ipa/I/O",
    "Flow:/qxm_lpass/I/O",
    "Flow:/qxm_lpass_q6/I/O",
    "Flow:/qxm_mss/I/O",
    "Flow:/qxm_mss_nav/I/O",
    "Flow:/qxm_ocmem_dma/I/O",
    "Flow:/qxm_pnoc/I/O",
    "Flow:/xm_pcie_0/I/O",
    "Flow:/xm_qdss_etr/I/O",
    "Flow:/xm_usb3_0/I/O"
]

SNoC_TargFlow = [
    "Flow:/qhs0/T/kpss",
    "Flow:/qhs0/T/kpss_bus_timeout",
    "Flow:/qhs1/T/lpass",
    "Flow:/qhs1/T/lpass_bus_timeout",
    "Flow:/qhs2/T/usb3_0",
    "Flow:/qhs2/T/usb3_0_bus_tiemout",
    "Flow:/qxs0_bimc/T/O",
    "Flow:/qxs1_bimc/T/O",
    "Flow:/qxs_cnoc/T/O",
    "Flow:/qxs_imem/T/O",
    "Flow:/qxs_ocmem/T/O",
    "Flow:/qxs_pcie_0/T/O",
    "Flow:/qxs_pnoc/T/O",
    "Flow:/srvc_snoc/T/O",
    "Flow:/xs_qdss_stm/T/O",
    "RESERVED"
]

CNoC_InitFlow = [
    "Flow:/qhm0_rpm/I/M0",
    "Flow:/qhm0_rpm/I/M2",
    "Flow:/qhm1/I/bimc_dehr",
    "Flow:/qhm1/I/spdm",
    "Flow:/qhm2_tic/I/O",
    "Flow:/qxm_snoc/I/O",
    "Flow:/xm_qdss_dap/I/O",
    "RESERVED"
]

CNoC_TargFlow = [
    "Flow:/qhs0/T/bus_timeout",
    "Flow:/qhs0/T/clk_ctl",
    "Flow:/qhs0/T/ipa_cfg",
    "Flow:/qhs0/T/security",
    "Flow:/qhs0/T/tcsr",
    "Flow:/qhs0/T/tlmm",
    "RESERVED",
    "RESERVED",
    "Flow:/qhs2/T/boot_rom",
    "Flow:/qhs2/T/bus_timeout",
    "Flow:/qhs2/T/mesg_ram",
    "Flow:/qhs2/T/mpm",
    "Flow:/qhs2/T/pmic_arb",
    "Flow:/qhs2T/spdm",
    "RESERVED",
    "RESERVED",
    "Flow:/qhs3/T/bimc_dehr_cfg",
    "Flow:/qhs3/T/bus_timeout",
    "Flow:/qhs3/T/qdss_cfg",
    "Flow:/qhs3/T/qdss_rbcpr_apu_cfg",
    "Flow:/qhs3/T/rbcpr_cx",
    "Flow:/qhs3/T/rbcpr_mx",
    "RESERVED",
    "RESERVED",
    "Flow:/qhs4/T/bus_timeout",
    "Flow:/qhs4/T/mmss_cfg",
    "Flow:/qhs4/T/mnoc_cfg",
    "Flow:/qhs4/T/pnoc_cfg",
    "Flow:/qhs4/T/snoc_cfg",
    "Flow:/qhs4/T/snoc_mpu_cfg",
    "RESERVED",
    "RESERVED",
    "Flow:/qhs1/T/bimc_cfg",
    "Flow:/qhs1/T/bus_timeout",
    "Flow:/qhs1/T/crypto0_cfg",
    "Flow:/qhs1/T/crypto1_cfg",
    "Flow:/qhs1/T/imem_cfg",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "Flow:/qhs5/T/bus_timeout",
    "Flow:/qhs5/T/ebi1_phy_apu_cfg",
    "Flow:/qhs5/T/ebi1_phy_cfg",
    "RESERVED",
    "Flow:/qhs7/T/bus_timeout",
    "Flow:/qhs7/T/mss_cfg",
    "Flow:/qhs7/T/pcie0_cfg",
    "RESERVED",
    "Flow:/qhs8/T/bus_timeout",
    "Flow:/qhs8/T/rpm",
    "Flow:/qxs_snoc/T/O",
    "Flow:/srvc_cnoc/T/O",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "RESERVED",
]

CNoC_InitFlow = [
    "Flow:/periph_noc_spec/qhm1/I/blsp1",
    "Flow:/periph_noc_spec/qhm1/I/blsp2",
    "Flow:/periph_noc_spec/qhm1/I/tsif",
    "Flow:/periph_noc_spec/qhm0_usb_hs1/I/O",
    "Flow:/periph_noc_spec/qhm_pnoc_cfg/I/O",
    "Flow:/periph_noc_spec/qxm_snoc/I/O",
    "Flow:/periph_noc_spec/xm_sdc1/I/O",
    "Flow:/periph_noc_spec/xm_sdc2/I/O",
    "Flow:/periph_noc_spec/xm_sdc3/I/O",
    "Flow:/periph_noc_spec/xm_sdc4/I/O",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "RESERVED",
]

PNoC_TargFlow = [
    "Flow:/periph_noc_spec/qhs4/T/ahb2phy_cfg",
    "Flow:/periph_noc_spec/qhs4/T/bus_timeout",
    "Flow:/periph_noc_spec/qhs4/T/pdm",
    "Flow:/periph_noc_spec/qhs4/T/periph_apu_cfg",
    "Flow:/periph_noc_spec/qhs4/T/pnoc_mpu_cfg",
    "Flow:/periph_noc_spec/qhs4/T/prng",
    "RESERVED",
    "RESERVED",
    "Flow:/periph_noc_spec/qhs1/T/blsp1",
    "Flow:/periph_noc_spec/qhs1/T/bus_timeout",
    "Flow:/periph_noc_spec/qhs1/T/sdc2",
    "Flow:/periph_noc_spec/qhs1/T/sdc4",
    "Flow:/periph_noc_spec/qhs1/T/tsif",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "Flow:/periph_noc_spec/qhs0/T/bus_timeout",
    "Flow:/periph_noc_spec/qhs0/T/sdc1",
    "Flow:/periph_noc_spec/qhs0/T/sdc3",
    "RESERVED",
    "Flow:/periph_noc_spec/qhs2/T/blsp2",
    "Flow:/periph_noc_spec/qhs2/T/bus_timeout",
    "Flow:/periph_noc_spec/qhs3/T/bus_timeout",
    "Flow:/periph_noc_spec/qhs3/T/usb_hs1",
    "Flow:/periph_noc_spec/qxs_snoc/T/O",
    "Flow:/periph_noc_spec/srvc_pnoc/T/O",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "RESERVED",
]


MNoC_InitFlow = [
    "Flow:/qhm_cnoc/I/O",
    "Flow:/qhm_mnoc_cfg/I/O",
    "Flow:/qxm_cpp/I/O",
    "Flow:/qxm_fd/I/O",
    "Flow:/qxm_gemini/I/O",
    "Flow:/qxm_mdp0/I/O",
    "Flow:/qxm_mdp1/I/O",
    "Flow:/qxm_venus0/I/O",
    "Flow:/qxm_venus1/I/O",
    "Flow:/qxm_vfe/I/O",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "RESERVED",
]

MNoC_TargFlow = [
    "Flow:/qhs_mmss/T/bus_timeout",
    "Flow:/qhs_mmss/T/camera_ss_cfg",
    "Flow:/qhs_mmss/T/mmss_cpr_cfg",
    "Flow:/qhs_mmss/T/mmss_cpr_xpu_cfg",
    "Flow:/qhs_mmss/T/ocmem_cfg",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "Flow:/qhs_mmss1/T/bus_timeout",
    "Flow:/qhs_mmss1/T/mmss_misc_cfg",
    "Flow:/qhs_mmss1/T/mmss_misc_xpu_cfg",
    "Flow:/qhs_mmss1/T/oxili_cfg",
    "Flow:/qhs_mmss1/T/venus0_cfg",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "Flow:/qhs_mmss2/T/bus_timeout",
    "Flow:/qhs_mmss2/T/mmss_clk_cfg",
    "Flow:/qhs_mmss2/T/mmss_clk_xpu_cfg",
    "Flow:/qhs_mmss2/T/mnoc_mpu_cfg",
    "Flow:/qhs_mmss3/T/bus_timeout",
    "Flow:/qhs_mmss3/T/disp_ss_cfg",
    "Flow:/qhs_mmss3/T/fd_cfg",
    "Flow:/qxs0_bimc/T/O",
    "Flow:/qxs1_bimc/T/O",
    "Flow:/srvc_mnoc/T/O",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "RESERVED",
    "RESERVED",
]


def get_field(line, msb, lsb):
    val = (eval(line.split()[4]) >> lsb) & (2**(msb - lsb + 1) - 1)
    return val

def do_noc_dec(tzdump):
    out = open("./out/tzdump_noc.txt", "w")

    infile = open(tzdump, "r")
    lines = infile.readlines()

    for line in lines:
        errcode = 0
        if "SNOC ERROR" in line:

            if "ERRLOG0" in line:
                out.write(line)

                errcode = get_field(line, 10, 8)
                if errcode == 0:
                    out.write("  Error detected by the slave with no information or no error reason\n")
                elif errcode == 1:
                    out.write("  Decode error\n")
                elif errcode == 3:
                    out.write("  Disconnected target or NoC domain\n")

            elif "ERRLOG1" in line:
                out.write(line)

                InitFlow = get_field(line, 31, 28)
                out.write("  InitFlow(master): " + SNoC_InitFlow[InitFlow] + "\n")

                TargFlow = get_field(line, 27, 24)
                out.write("  TargFlow(slave):  " + SNoC_TargFlow[TargFlow] + "\n")

                TargSubRange = get_field(line, 23, 22)
                out.write("  TargSubRange:     " + hex(TargSubRange) + "\n")

                SrcidBID = get_field(line, 16, 14)
                out.write("  Srcid.BID:        " + hex(SrcidBID) + " (" + BID[SrcidBID] + ")\n")

                SrcidPID = get_field(line, 21, 17)
                out.write("  Srcid.PID:        " + hex(SrcidPID))

                # BIMC master IDs
                if SrcidBID == 2:
                    if SrcidPID == 3:
                        out.write(" (A53 cluster)")
                    elif SrcidPID == 4:
                        out.write(" (A57 cluster)")
                    elif SrcidPID == 7:
                        out.write(" (CCI)")
                    elif SrcidPID == 8:
                        out.write(" (GPU)")
                    elif SrcidPID == 9:
                        out.write(" (Modem Hexagon)")
                out.write("\n")

                SrcidMID = get_field(line, 13, 6)
                out.write("  Srcid.MID:        " + hex(SrcidMID) + "\n")

                SeqId = get_field(line, 5, 0)
                out.write("  SeqId:            " + hex(SeqId) + "\n")

            elif "ERRLOG2" in line:
                out.write(line)

                RouteID = eval(line.split()[4])
                out.write("  RouteID:          " + hex(RouteID) + "\n")

            elif "ERRLOG3" in line:
                out.write(line)

                address_offset = eval(line.split()[4])
                if errcode == 1:
                    out.write("  address:          " + hex(address_offset) + "\n")
                else:
                    out.write("  offset:           " + hex(address_offset) + "\n")

            elif "ERRLOG4" in line:
                out.write(line)

                address_offset = eval(line.split()[4])
                if errcode == 1:
                    out.write("  address:          " + hex(address_offset) + "\n")
                else:
                    out.write("  offset:           " + hex(address_offset) + "\n")

            elif "ERRLOG5" in line:
                out.write(line)

    out.write("\n")
    infile.close()

    infile = open(tzdump, "r")
    lines = infile.readlines()

    for line in lines:
        errcode = 0
        if "CNOC ERROR" in line:

            if "ERRLOG0" in line:
                out.write(line)

                errcode = get_field(line, 10, 8)
                if errcode == 0:
                    out.write("  Error detected by the slave with no information or no error reason\n")
                elif errcode == 1:
                    out.write("  Decode error\n")
                elif errcode == 3:
                    out.write("  Disconnected target or NoC domain\n")

            elif "ERRLOG1" in line:
                out.write(line)

                InitFlow = get_field(line, 28, 26)
                out.write("  InitFlow(master): " + CNoC_InitFlow[InitFlow] + "\n")

                TargFlow = get_field(line, 25, 20)
                out.write("  TargFlow(slave):  " + CNoC_TargFlow[TargFlow] + "\n")

                TargSubRange = get_field(line, 19, 19)
                out.write("  TargSubRange:     " + hex(TargSubRange) + "\n")

                SrcidBID = get_field(line, 13, 11)
                out.write("  Srcid.BID:        " + hex(SrcidBID) + " (" + BID[SrcidBID] + ")\n")

                SrcidPID = get_field(line, 18, 14)
                out.write("  Srcid.PID:        " + hex(SrcidPID))

                # BIMC master IDs
                if SrcidBID == 2:
                    if SrcidPID == 3:
                        out.write(" (A53 cluster)")
                    elif SrcidPID == 4:
                        out.write(" (A57 cluster)")
                    elif SrcidPID == 7:
                        out.write(" (CCI)")
                    elif SrcidPID == 8:
                        out.write(" (GPU)")
                    elif SrcidPID == 9:
                        out.write(" (Modem Hexagon)")
                out.write("\n")

                SrcidMID = get_field(line, 10, 3)
                out.write("  Srcid.MID:        " + hex(SrcidMID) + "\n")

                SeqId = get_field(line, 2, 0)
                out.write("  SeqId:            " + hex(SeqId) + "\n")

            elif "ERRLOG2" in line:
                out.write(line)

                RouteID = eval(line.split()[4])
                out.write("  RouteID:          " + hex(RouteID) + "\n")

            elif "ERRLOG3" in line:
                out.write(line)

                address_offset = eval(line.split()[4])
                if errcode == 1:
                    out.write("  address:          " + hex(address_offset) + "\n")
                else:
                    out.write("  offset:           " + hex(address_offset) + "\n")

            elif "ERRLOG4" in line:
                out.write(line)

                address_offset = eval(line.split()[4])
                if errcode == 1:
                    out.write("  address:          " + hex(address_offset) + "\n")
                else:
                    out.write("  offset:           " + hex(address_offset) + "\n")

            elif "ERRLOG5" in line:
                out.write(line)

    out.write("\n")
    infile.close()

    infile = open(tzdump, "r")
    lines = infile.readlines()

    for line in lines:
        errcode = 0
        if "PNOC ERROR" in line:

            if "ERRLOG0" in line:
                out.write(line)

                errcode = get_field(line, 10, 8)
                if errcode == 0:
                    out.write("  Error detected by the slave with no information or no error reason\n")
                elif errcode == 1:
                    out.write("  Decode error\n")
                elif errcode == 3:
                    out.write("  Disconnected target or NoC domain\n")

            elif "ERRLOG1" in line:
                out.write(line)

                InitFlow = get_field(line, 28, 25)
                out.write("  InitFlow(master): " + PNoC_InitFlow[InitFlow] + "\n")

                TargFlow = get_field(line, 24, 20)
                out.write("  TargFlow(slave):  " + PNoC_TargFlow[TargFlow] + "\n")

                TargSubRange = get_field(line, 19, 19)
                out.write("  TargSubRange:     " + hex(TargSubRange) + "\n")

                SrcidBID = get_field(line, 13, 11)
                out.write("  Srcid.BID:        " + hex(SrcidBID) + " (" + BID[SrcidBID] + ")\n")

                SrcidPID = get_field(line, 18, 14)
                out.write("  Srcid.PID:        " + hex(SrcidPID))

                # BIMC master IDs
                if SrcidBID == 2:
                    if SrcidPID == 3:
                        out.write(" (A53 cluster)")
                    elif SrcidPID == 4:
                        out.write(" (A57 cluster)")
                    elif SrcidPID == 7:
                        out.write(" (CCI)")
                    elif SrcidPID == 8:
                        out.write(" (GPU)")
                    elif SrcidPID == 9:
                        out.write(" (Modem Hexagon)")
                out.write("\n")

                SrcidMID = get_field(line, 10, 3)
                out.write("  Srcid.MID:        " + hex(SrcidMID) + "\n")

                SeqId = get_field(line, 2, 0)
                out.write("  SeqId:            " + hex(SeqId) + "\n")

            elif "ERRLOG2" in line:
                out.write(line)

                RouteID = eval(line.split()[4])
                out.write("  RouteID:          " + hex(RouteID) + "\n")

            elif "ERRLOG3" in line:
                out.write(line)

                address_offset = eval(line.split()[4])
                if errcode == 1:
                    out.write("  address:          " + hex(address_offset) + "\n")
                else:
                    out.write("  offset:           " + hex(address_offset) + "\n")

            elif "ERRLOG4" in line:
                out.write(line)

                address_offset = eval(line.split()[4])
                if errcode == 1:
                    out.write("  address:          " + hex(address_offset) + "\n")
                else:
                    out.write("  offset:           " + hex(address_offset) + "\n")

            elif "ERRLOG5" in line:
                out.write(line)

    out.write("\n")
    infile.close()

    infile = open(tzdump, "r")
    lines = infile.readlines()

    for line in lines:
        errcode = 0
        if "MNOC ERROR" in line:

            if "ERRLOG0" in line:
                out.write(line)

                errcode = get_field(line, 10, 8)
                if errcode == 0:
                    out.write("  Error detected by the slave with no information or no error reason\n")
                elif errcode == 1:
                    out.write("  Decode error\n")
                elif errcode == 3:
                    out.write("  Disconnected target or NoC domain\n")

            elif "ERRLOG1" in line:
                out.write(line)

                InitFlow = get_field(line, 31, 28)
                out.write("  InitFlow(master): " + MNoC_InitFlow[InitFlow] + "\n")

                TargFlow = get_field(line, 27, 23)
                out.write("  TargFlow(slave):  " + MNoC_TargFlow[TargFlow] + "\n")

                TargSubRange = get_field(line, 22, 22)
                out.write("  TargSubRange:     " + hex(TargSubRange) + "\n")

                SrcidBID = get_field(line, 16, 14)
                out.write("  Srcid.BID:        " + hex(SrcidBID) + " (" + BID[SrcidBID] + ")\n")

                SrcidPID = get_field(line, 21, 17)
                out.write("  Srcid.PID:        " + hex(SrcidPID))

                # BIMC master IDs
                if SrcidBID == 2:
                    if SrcidPID == 3:
                        out.write(" (A53 cluster)")
                    elif SrcidPID == 4:
                        out.write(" (A57 cluster)")
                    elif SrcidPID == 7:
                        out.write(" (CCI)")
                    elif SrcidPID == 8:
                        out.write(" (GPU)")
                    elif SrcidPID == 9:
                        out.write(" (Modem Hexagon)")
                out.write("\n")

                SrcidMID = get_field(line, 13, 6)
                out.write("  Srcid.MID:        " + hex(SrcidMID) + "\n")

                SeqId = get_field(line, 5, 0)
                out.write("  SeqId:            " + hex(SeqId) + "\n")

            elif "ERRLOG2" in line:
                out.write(line)

                RouteID = eval(line.split()[4])
                out.write("  RouteID:          " + hex(RouteID) + "\n")

            elif "ERRLOG3" in line:
                out.write(line)

                address_offset = eval(line.split()[4])
                if errcode == 1:
                    out.write("  address:          " + hex(address_offset) + "\n")
                else:
                    out.write("  offset:           " + hex(address_offset) + "\n")

            elif "ERRLOG4" in line:
                out.write(line)

                address_offset = eval(line.split()[4])
                if errcode == 1:
                    out.write("  address:          " + hex(address_offset) + "\n")
                else:
                    out.write("  offset:           " + hex(address_offset) + "\n")

            elif "ERRLOG5" in line:
                out.write(line)

    out.write("\n")
    infile.close()

    out.close()

def tzdump_noc(tzdump):
    do_noc_dec(tzdump)
