# =======================================================
# hagisiro.py
# -------------------------------------------------------
# 2013/04/16 v0.1 Initial release by darkwood.kim
# =======================================================
import sys
import os

def do_parse_rtb():
    max_cpus = 8
    end_lines = 30

    output_file = open("./last_rtb.txt", "w")

    for i in range(max_cpus):
        file_name = "msm_rtb" + str(i) + ".txt"
        file = open(file_name)
        lines = file.readlines()
        file.close()

        print_seperator(output_file, "=")
        output_file.write(file_name + "\n")
        print_seperator(output_file, "-")
        for j in lines[-end_lines :]:
            output_file.write(j)
        output_file.write("\n")

    output_file.close()

def print_seperator(output_file, str):
    for i in range(230):
        output_file.write(str)
    output_file.write("\n")

if __name__ == "__main__":
    do_parse_rtb()
