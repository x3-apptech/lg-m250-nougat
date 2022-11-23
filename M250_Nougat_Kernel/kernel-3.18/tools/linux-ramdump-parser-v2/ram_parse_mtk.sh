#!/bin/sh

DEFAULT_OPTION="
--print-ramoops
--clock-dump
--cpr-info
--cpu-state
--ddr-compare
--dmesg
--print-iommu-pg-tables
--print-irqs
--print-pagetypeinfo
--reset-reason
--check-rodata
--print-rtb
--print-tasks
--check-for-panic
--tz-log
--print-vmalloc
--print-vmstats
--print-workqueues
--print-runqueues"
#--parse-debug-image
#--print-kconfig
#--dump-page-tables
#--print-pagealloccorruption
#--print-pagetracking

if [ -d ./out ]; then
	echo ===== clean ================================================================
	rm -rvf ./out
fi

# if true; then
# 	echo ===== vmlinux ==============================================================
# 	strings ../vmlinux | grep "Linux version"
# 	echo ===== DDRCS0_0.BIN =========================================================
# 	strings DDRCS0.BIN | grep "Linux version"
# fi
mkdir out
if [ $1 ]; then
	echo ============================================================================
	echo select option :
	echo $@
	echo ============================================================================
	python $HOME/tools/mtk_ramparser/tools/linux-ramdump-parser-v2/ramparse.py ${@} -o out -e ./ramdump.bin 0x40000000 0xbfffffff --phys-offset 0x40000000 -a . -v ../vmlinux --32-bit |tee ramparse_result.txt
else
	echo ============================================================================
	echo default option :
	echo $DEFAULT_OPTION
	echo ============================================================================
	python $HOME/tools/mtk_ramparser/tools/linux-ramdump-parser-v2/ramparse.py ${DEFAULT_OPTION} -o out -e ./ramdump.bin 0x40000000 0xbfffffff --phys-offset 0x40000000 -a . -v ../vmlinux --32-bit|tee ramparse_result.txt
fi

# if [ -f ../RPM_AAAAANAAR.elf ]; then
# 	echo ===== rpm hansei parser ====================================================
# 	python ${HOME}/tools/mtk_ramparser/tools/hansei/hansei.py --elf ../RPM_AAAAANAAR.elf -o ./out/rpm CODERAM.BIN DATARAM.BIN MSGRAM.BIN OCIMEM.BIN|tee ramparse_result.txt
# else
# 	echo RPM_AAAAANAAR.elf not exists
# fi
