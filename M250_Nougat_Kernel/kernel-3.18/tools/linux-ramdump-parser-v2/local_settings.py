import platform
import re
import sys
import os

def get_system_type() :
    plat = platform.system()

    if plat == 'Windows' :
       return 'Windows'
    if re.search('CYGWIN',plat) is not None :
       # On certain installs, the default windows shell
       # runs cygwin. Treat cygwin as windows for this
       # purpose
       return 'Windows'
    if plat == 'Linux' :
       return 'Linux'

    if plat == 'Darwin' :
       return 'Darwin'

    print_out_str ("[!!!] This is a target I don't recognize!")
    print_out_str ("[!!!] Some features may not work as expected!")
    print_out_str ("[!!!] Assuming Linux...")

system_type = get_system_type()
path = os.path.dirname(os.path.abspath(__file__))

if system_type is 'Windows':
    path = path + "\\toolchain\\windows\\"
    gdb_path = path + "arm-none-eabi-gdb.exe"
    nm_path = path + "arm-none-eabi-nm.exe"
    objdump_path = path + "all-objdump.exe"
    gdb64_path = path + "aarch64-linux-gnu-gdb.exe"
    nm64_path = path + "aarch64-linux-gnu-nm.exe"
    objdump64_path = path + "all-objdump.exe"
else :
    path = path + "/toolchain/linux/"
    gdb_path = path + "arm-linux-androideabi-gdb"
    nm_path = path + "arm-linux-androideabi-nm"
    objdump_path = path + "arm-linux-androideabi-objdump"
    # gdb_path = path + "arm-eabi-gdb"
    # nm_path = path + "arm-eabi-nm"
    # objdump_path = path + "arm-eabi-objdump"
    gdb64_path = path + "aarch64-linux-android-gdb"
    nm64_path = path + "aarch64-linux-android-nm"
    objdump64_path = path + "aarch64-linux-android-objdump"
    qtf_path = "/home/darkwood.kim/QTF0.6.7/bin/qtf.sh"
