#!/usr/bin/env python

# vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
from __future__ import print_function

import os
import serial
import serial.tools.list_ports
import array
import sys
import struct
import time
import platform
import argparse
import time
from time import localtime, strftime
from datetime import datetime
import commands
import base64
import ConfigParser

def print(*args, **kwargs):
    __builtins__.print(datetime.strftime(datetime.now(), '%Y-%m-%d %H:%M:%S'), ": ", end="")
    return __builtins__.print(*args, **kwargs)


class SerialHelper():
    def __init__(self, connection, baudrate = 9600):
        self.serial = self._OpenSerialDevice(connection, baudrate)

    ############################################################################
    #
    #
    #
    ############################################################################
    def _OpenSerialDevice(self, connection, baudrate):
        try:
            #handle = serial.Serial(connection, baudrate, timeout = 1)
            # add rtscts=True,dsrdtr=True because of pyserial bug
            handle = serial.Serial(connection, baudrate, timeout = 1, rtscts=True,dsrdtr=True)
        except (serial.SerialException, ValueError) as e:
            print(e)
            return None

        return handle

    ############################################################################
    #
    #
    #
    ############################################################################
    def SendBytes(self, data):
        try:
            ret = self.serial.write(data)
        except serial.SerialException as e:
            print(e)
            return False

        if ret != len(data):
            print("%s : write error" % sys._getframe().f_code.co_name)
            return False

        return True

    ############################################################################
    #
    #
    #
    ############################################################################
    def ReceiveBytes(self, len = 1):
        try:
            ret = self.serial.read(size = len)
        except serial.SerialException as e:
            print(e)
            return None

        return ret

    ############################################################################
    #
    #
    #
    ############################################################################
    def ExchangeByte(self, data):
        try:
            if self.serial.write(chr(data)) != 1:
                print("%s : write error" % sys._getframe().f_code.co_name)
                return False
        except serial.SerialException as e:
            print(e)
            return False

        try:
            ret = self.serial.read(size=1)
        except serial.SerialException as e:
            print(e)
            return False

        if ret != chr(data):
            print("%s : read error" % sys._getframe().f_code.co_name)
            return False

        return True

    ############################################################################
    #
    # write first, read later
    #
    ############################################################################
    def EchoInteger(self, data):
        bData = struct.pack('I', data)
        fData = bData[3] + bData[2] + bData[1] + bData[0]

        try:
            self.serial.write(fData)
            recv = self.serial.read(size=4)
            result = int(recv.encode("hex"), 16)
        except serial.SerialException as e:
            print(e)
            return False

        if result != data:
            print("%s : Read Error" % sys._getframe().f_code.co_name)
            return False

        return True

    ############################################################################
    #
    # read first, write later (opposite with EchoInteger)
    #
    ############################################################################
    def ExchangeInteger(self):
        try:
            recv = self.serial.read(size = 4)
            result = int(recv.encode("hex"), 16)
            bData = struct.pack('I', result)
            fData = bData[3] + bData[2] + bData[1] + bData[0]
            self.serial.write(fData)
        except serial.SerialException as e:
            print(e)
            return False, -1

        return True, result

    ############################################################################
    #
    #
    #
    ############################################################################
    def ExchangeDwordReverse(self):
        recv = self.serial.read(size = 4)
        result = int(recv.encode("hex"), 16)

        bData = struct.pack('I', result)
        fData = bData[3] + bData[2] + bData[1] + bData[0]
        self.serial.write(fData)

        return result

    ############################################################################
    #
    #
    #
    ############################################################################
    def ReceiveInteger(self):
        recv = self.serial.read(size=4)

        if recv == None:
            print("%s : Receive Error" % sys._getframe().f_code.co_name)
            return None

        result = int(recv.encode("hex"),16)

        return result

    ############################################################################
    #
    #
    #
    ############################################################################
    def ReceiveQword(self):
        result = self.serial.read(size=4096)

        if result == None:
            print("%s : Receive Error" % sys._getframe().f_code.co_name)
            return None

        if len(result) != 4096:
            print("result = ", len(result))

        return result

    ############################################################################
    #
    #
    #
    ############################################################################
    def ReceiveBulk(self):
        try:
            result = self.serial.read(size=4096)
        except (serial.SerialException, ValueError) as e:
            print(e)

        if result == None:
            return None, 0

        return result, len(result)

    ############################################################################
    #
    #
    #
    ############################################################################
    def Flush(self):
        self.serial.flush()

class Dumper():
    def __init__(self, config = None):
        self.start_code = ((0xa0, 0x5f), (0x0a, 0xf5), (0x50, 0xaf), (0x05, 0xfa))
        self.cmd_mem_info = 0x31
        self.cmd_reg_dump = 0xA0
        self.cmd_ddr_dump = 0xD1
        self.cmd_hdr_dump = 0xC8
        self.cmd_boot = 0xB0
        self.cmd_get_serial = 0x12
        self.cmd_get_oops = 0x21
        self.cmd_fb_addr = 0xFA
        self.cmd_get_last_pc = 0x9C
        self.cmd_get_spm_dump = 0xE4
        self.cmd_finish = 0xED
        self.cmd_addition = 0x40
        self.cmd_dumper_version = 0x39

        self.dumper_version = "0.2.2"

        self.ddr_start_addr = 0
        self.ddr_total_size = 0

        self.config = config
        self.dir_date_path = strftime("%Y%m%d%H%M%S", localtime())

        if self.config.out == None:
            self.config.out = os.path.join(os.getcwd(), self.dir_date_path)

        if self.config.connection == None:
            p_list = list(serial.tools.list_ports.grep("1004:6340"))

            if len(p_list) > 0:
                print("%s will be dumped" % p_list[0][0])
                self.handle = SerialHelper(p_list[0][0], baudrate = 9600)
                if self.handle == None:
                    self._setStatus(False)
                    return
            else:
                print("device is not found")
                self._SetStatus(False)
                return
        else:
            self.handle = SerialHelper(self.config.connection, baudrate = 9600)
            if self.handle == None:
                self._SetStatus(False)
                return

        self._SetStatus(True)

    ############################################################################
    #
    #
    #
    ############################################################################
    def __del__(self):
        #del self.handle
        pass

    ############################################################################
    #
    #
    #
    ############################################################################
    def _SetStatus(self, flag):
        self.status = flag

    ############################################################################
    #
    #
    #
    ############################################################################
    def GetStatus(self):
        return self.status

    ############################################################################
    #
    # 
    #
    ############################################################################
    def StartRamDumpProcess(self):
        print(sys._getframe().f_code.co_name)

        #retry = 0
        #while retry < 5:
        #    if self._SendReadyCommand():
        #        break;
        #    else:
        #        retry += 1
#
#        if retry >= 5:
#            return False
#
        buffer = 'ready'
        self.handle.serial.write(buffer)
        #buffer = self.handle.serial.read(size=len(buffer))
        #print buffer
        #return False


        if self._SendStartCommand(self.start_code) is False:
            print("%s was filed" % sys._getframe().f_code.co_name)
            return False

        print("will be dumped in %s" % (self.config.out))
		
        if self._StartDumperVersion() is False:
			return False
		
        os.makedirs(self.config.out)

        self._StartGetSerial()

        self._StartGetMemInfo()

        self._StartGetSPMDUMP()

        self._StartGetDDR("smem.bin", 0x46400000, 0x40000)

        self._StartGetDDR("ramdump.bin", self.ddr_start_addr, self.ddr_total_size)

        self._StartGetRegInfo()

        self._StartGetOops()

        self._StartGetFB()

        self._StartGetLastPC()

        self._StartAdditionalCMD()

        #if self.config.reset is True:
        #    self._SendResetCommand()
        #else: 
        #    self._SendFinishCommand()
        return True

    ############################################################################
    #
    #
    #
    ############################################################################
    def _SendReadyCommand(self):
        if self.handle.SendBytes('ready') == True:
            buffer = self.handle.ReceiveBytes(5)

            if buffer == "ready":
                return True
            else:
                print("ready command completed? : %s\n" % buffer)
                return False

    def _SendStartCommand(self, start_code):
        for s,r in start_code:
            if self.handle.SendBytes(chr(s)) == False:
                return False
            rec = self.handle.ReceiveBytes()

            if rec != chr(r):
                print("%s : Unmatched with Start Code"
                        % sys._getframe().f_code.co_name)
                print("receive = ", rec, ", send = ", chr(r))
                return False

            print("%s Success " % sys._getframe().f_code.co_name)

        return True

    ############################################################################
    #
    #
    #
    ############################################################################
    def _SendResetCommand(self):
        print("%s : send reset command" % sys._getframe().f_code.co_name)

        if self.handle.ExchangeByte(self.cmd_boot) is False:
            print("%s : Commnad Failed" % sys._getframe().f_code.co_name)

			
    ############################################################################
    #
    #
    #
    ############################################################################
    def _StartDumperVersion(self):
        print("Dumper version is ", self.dumper_version)
        if self.handle.ExchangeByte(self.cmd_dumper_version) is False:
            print("%s : Commnad Failed" % sys._getframe().f_code.co_name)
            return True  # if this cmd does not exist, it supports!

        version_len = self.handle.ReceiveInteger()
        self.handle.SendBytes(self.dumper_version)

        result = self.handle.ReceiveInteger()
        if result is 0x1:
            return True
        print("dumper version is not supported!!!")
        return False


    ############################################################################
    #
    #
    #
    ############################################################################
    def _StartGetSerial(self):
        if self.handle.ExchangeByte(self.cmd_get_serial) is False:
            print("%s : Commnad Failed" % sys._getframe().f_code.co_name)
            return False

        serial_len = self.handle.ReceiveInteger()
        serial = self.handle.ReceiveBytes(serial_len)
        print("serial = %s\n" % (serial))

        f = file(os.path.join(self.config.out, "serial_number.txt"), "w")
        f.write(serial)
        f.write('\n')
        f.close()

        return True

    ############################################################################
    #
    #
    #
    ############################################################################
    def _SendFinishCommand(self):
        print("%s : Send Finsish Command" % sys._getframe().f_code.co_name)

        if self.handle.ExchangeByte(self.cmd_finish) is False:
            print("%s : Commnad Failed" % sys._getframe().f_code.co_name)


    def _StartGetMemInfo(self):
        if self.handle.ExchangeByte(self.cmd_mem_info) is False:
            print("%s : Commnad Failed" % sys._getframe().f_code.co_name)
            return False

        self.ddr_start_addr = self.handle.ReceiveInteger()
        bData = struct.pack('I', self.ddr_start_addr)
        fData = bData[3] + bData[2] + bData[1] + bData[0]
        self.handle.serial.write(fData)

        self.ddr_total_size = self.handle.ReceiveInteger()
        bData = struct.pack('I', self.ddr_total_size)
        fData = bData[3] + bData[2] + bData[1] + bData[0]
        self.handle.serial.write(fData)

        return True

    def _StartGetDDR(self, file_name, start_addr, size):
        # init data
        recv_num    = 0
        SIZE_ONE_TIME  = 25 * 1024 * 1024
        progress = '|\-/'
        progress_idx = 0

        f = file(os.path.join(self.config.out, file_name), "wb")

        start_addr = start_addr
        length = size
        end_addr = start_addr + length


        curr_addr = start_addr

        print("Start address = 0x%x, Total Size = 0x%x" % (start_addr, size))

        while  curr_addr < end_addr:
            if self.handle.ExchangeByte(self.cmd_ddr_dump) is False:
                print("%s : Commnad Failed" % sys._getframe().f_code.co_name)
                return False

            ret = self.handle.EchoInteger(curr_addr)
            if ret is False:
                print("%s : Address Send Failed" % sys._getframe().f_code.co_name)
                return False

            if (curr_addr + SIZE_ONE_TIME) < end_addr:
                loop_length = SIZE_ONE_TIME
            else:
                loop_length = end_addr - curr_addr

            #print("curr_addr = ", curr_addr, " loop_length = ", loop_length)

            if self.handle.EchoInteger(loop_length) is False:
                print("%s : Length Send Failed" % sys._getframe().f_code.co_name)

            #print("Starting dumping..")

            total_received = 0
            buffer = bytearray()
            flag = True

            while total_received < loop_length:
                raw, len = self.handle.ReceiveBulk()
                #print len
                if raw != None and len != 0:
                    total_received += len
                    buffer.extend(raw)
                else:
                    flag = False
                    break

            if flag == True:
                #self.queue.put(buffer, block = True)
                f.write(buffer)
                curr_addr += loop_length
                #print("total_received = %d" % (total_received))
                percent = (curr_addr - start_addr) * 100 / (end_addr - start_addr)
                #print("%d %%" % (percent))
                progress_string = progress[progress_idx % 4] + '%10s' % str(percent) + '%'
                sys.stdout.write('\r')
                sys.stdout.write(progress_string)
                sys.stdout.flush()
                progress_idx += 1
            else:
                #print("retry...")
                time.sleep(1)

            del(buffer)

        sys.stdout.write('\n')
        sys.stdout.write('\n')
        sys.stdout.flush()
        # error check is needed
        print("*** Finished ***")

        #self.queue.put("#DONE#")

        self._create_load_bin_cmm(start_addr)

        return True

    def _create_load_bin_cmm(self, ddr_start):
        f = file(os.path.join(self.config.out, "load_bin.cmm"), "w")

        f.write("sys.cpu CortexA7MPCore\n")
        f.write("sys.u\n")
        f.write("d.load.binary ramdump.bin A:")
        f.write(str(hex(ddr_start)))
        f.write("\nd.load.elf * /noclear")

        f.close()

    def _StartGetRegInfo(self):
        if self.handle.ExchangeByte(self.cmd_reg_dump) is False:
            print("%s : Commnad Failed" % sys._getframe().f_code.co_name)

        f = file(os.path.join(self.config.out, "reg_core.cmm"), "w")

        for j in range(0, 21):
            if j < 15:
                f.write("r.s ")
                tmp = "%x" % self.handle.ExchangeDwordReverse()
                f.write("r" + str(j) + " " + "0x" + tmp + "\n")
            elif j == 15:
                f.write("r.s ")
                tmp = "%x" % self.handle.ExchangeDwordReverse()
                f.write("pc " + "0x" + tmp + "\n")
            elif j == 16:
                f.write("r.s ")
                tmp = "%x" % self.handle.ExchangeDwordReverse()
                f.write("CPSR " + "0x" + tmp + "\n")
            elif j == 17:
                f.write("PER.S ")
                tmp = "%x" % self.handle.ExchangeDwordReverse()
                f.write("C15:0x1 " + "0x" + tmp + "\n")
            elif j == 18:
                f.write("PER.S ")
                tmp = "%x" % self.handle.ExchangeDwordReverse()
                f.write("C15:0x2 " + "0x" + tmp + "\n")
            elif j == 19:
                f.write("PER.S ")
                tmp = "%x" % self.handle.ExchangeDwordReverse()
                f.write("C15:0x102 " + "0x" + tmp + "\n")
            elif j == 20:
                f.write("PER.S ")
                tmp = "%x" % self.handle.ExchangeDwordReverse()
                f.write("C15:0x202 " + "0x" + tmp + "\n")

        f.write("mmu.off\n")
        f.write("mmu.on\n")
        f.write("mmu.scan\n")

        f.close()

    def _StartGetOops(self):
        if self.handle.ExchangeByte(self.cmd_get_oops) is False:
            print("%s : Commnad Failed" % sys._getframe().f_code.co_name)
            return False

        oops_addr = self.handle.ReceiveInteger()
        bData = struct.pack('I', oops_addr)
        fData = bData[3] + bData[2] + bData[1] + bData[0]
        self.handle.serial.write(fData)

        oops_size = self.handle.ReceiveInteger()
        bData = struct.pack('I', oops_size)
        fData = bData[3] + bData[2] + bData[1] + bData[0]
        self.handle.serial.write(fData)

        #print("Oops address = 0x%x, Oops Size = 0x%x\n" % (oops_addr, oops_size))

        f = file(os.path.join(self.config.out, "ramoops.txt"), "w")
        f.write(self.handle.ReceiveBytes(oops_size));

        f.close()

        return True

    def _StartGetFB(self):
        if self.handle.ExchangeByte(self.cmd_fb_addr) is False:
            print("%s : Commnad Failed" % sys._getframe().f_code.co_name)
            return False

        ret, fb_addr = self.handle.ExchangeInteger()
        if ret == False:
            print("fb address error")
            return False

        ret, fb_size = self.handle.ExchangeInteger()
        if ret == False:
            print("fb size error")
            return False

        ret, fb_x = self.handle.ExchangeInteger()
        if ret == False:
            print("fb x error")
            return False

        ret, fb_y = self.handle.ExchangeInteger()
        if ret == False:
            print("fb y error")
            return False

        #print("fb_addr = %x, fb_size = %d, fb_x = %d, fb_y = %d" % (fb_addr, fb_size, fb_x, fb_y))
        dfp = file(os.path.join(self.config.out, "ramdump.bin"), "rb")
        sfp = file(os.path.join(self.config.out, "display_dump.dat"), "wb")

        dfp.seek(fb_addr - self.ddr_start_addr, 0);
        sfp.write(dfp.read(fb_size));

        dfp.close()
        sfp.close()

        #convert -depth 8 -size fb_xxfb_y bgra:display_dump.dat -separate -combine screen.png

        return True;

    def _StartGetLastPC(self):
        if self.handle.ExchangeByte(self.cmd_get_last_pc) is False:
            print("%s : Commnad Failed" % sys._getframe().f_code.co_name)
            return False

        ret, length = self.handle.ExchangeInteger()
        if ret == False:
            print("last pc length error")
            return False

        if length == 0:
            print("last pc is 0\n")
            return True

        f = file(os.path.join(self.config.out, "lastpc.log"), "w")
        f.write(self.handle.ReceiveBytes(length));

        f.close()

        return True

    def _StartGetSPMDUMP(self):
        if self.handle.ExchangeByte(self.cmd_get_spm_dump) is False:
            print("%s : Commnad Failed" % sys._getframe().f_code.co_name)
            return False

        f = file(os.path.join(self.config.out, "spm.log"), "w")

        for j in range(1, 6):
            f.write("SPM_DEBUG")
            tmp = "0x%08x" % self.handle.ExchangeDwordReverse()
            f.write(str(j) + ":" + tmp + "\n")
        f.close()

        return True
	
    def _StartAdditionalCMD(self):
        max_string_length = 10
        if self.handle.ExchangeByte(self.cmd_addition) is False:
            print("%s : Command Failed" % sys._getframe().f_code.co_name)
            return False

        ret, count = self.handle.ExchangeInteger()
        if ret == False:
            print("get count error")
            return False
        if count == 0:
            print("count is 0\n")
            return True
		
        for j in range(0, count):
            filename = self.handle.ReceiveBytes(10)
            print("file name is", filename)
            ret, length = self.handle.ExchangeInteger()
            if ret == False:
                print("get length fail")
                return False

            f = file(os.path.join(self.config.out, filename), "w")
            f.write(self.handle.ReceiveBytes(length))
            f.close()
        return True

def StartRamdumpNormalMain(configs):
    dumper = Dumper(configs)

    if dumper.GetStatus() == True:
        if dumper.StartRamDumpProcess() == False:
            print("dumping is failed")
            sys.exit()
    else:
        print("Something error..")
        sys.exit()

if __name__ == "__main__":
    # check os
    plat = platform.system()

    parser = argparse.ArgumentParser(
            usage="%(prog)s [PortName | DeviceFilePath]",
            description="MTK RamDump Tool",
            epilog="Plz Don't call me")

    if plat == "Linux":
        parser.add_argument("-c", "--connection", help = "Path of USB Serial device file")
    elif plat == "Windows":
        parser.add_argument("-c", "--connection", help = "Port Name of Target", type=str)
    else:
        print("What are you using?")
        sys.exit()

    parser.add_argument("-o", "--out", help="Output Directory path", type=str)
    parser.add_argument("-r", "--reset", help="Reboot after dump", action="store_true")

    args = parser.parse_args()

    StartRamdumpNormalMain(args)
