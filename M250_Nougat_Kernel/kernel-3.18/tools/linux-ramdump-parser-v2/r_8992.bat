@echo off
set PARSE_LOCATION=D:\tool\dev\ramdump_parser\8992
REM set DEFAULT_OPTION=-d -p -r -t -q --tz-log --parse-debug-image
set DEFAULT_OPTION=-s --clock-dump --cpr-info --cpu-state --ddr-compare --parse-debug-image --dmesg --print-ramoops --print-irqs --print-kconfig --dump-page-tables --print-pagetypeinfo --reset-reason --check-rodata --print-rtb --print-tasks --check-for-panic --tz-log --print-vmalloc --print-vmstats --print-workqueues

if "%1" == "" (
	echo ============================================================================
	echo default option :
	echo %DEFAULT_OPTION%
	echo ============================================================================
	echo so below option is omitted.
	echo  --print-iommu-pg-tables      msm_iommu_domain_XX.txt
	echo  --print-pagealloccorruption  page_corruption_summary.txt / page_ranges.txt
	echo  --print-pagetracking         page_tracking.txt / page_frequency.txt
	echo ============================================================================

	C:\Python27\python "%PARSE_LOCATION%\ramparse.py" %DEFAULT_OPTION% -a . -v vmlinux --64-bit
) else (
	C:\Python27\python "%PARSE_LOCATION%\ramparse.py" %* -a . -v vmlinux --64-bit
)

C:\Python27\python "%PARSE_LOCATION%\scan843419_1.1\scan-843419.py vmlinux

explorer .
