1. Android build
  - Download original android source code ( android-7.0.0_r7 ) from http://source.android.com
  - Untar opensource packages of M250_Nougat_Android.tar.gz into downloaded android source directory
       a) tar -xvzf M250_Nougat_Android.tar.gz
  - And, merge the source into the android source code
  - Run following scripts to build android
    a) source build/envsetup.sh
    b) lunch 1
    c) make -j4
  - When you compile the android source code, you have to add google original prebuilt source(toolchain) into the android directory.
  - After build, you can find output at out/target/product/generic

2. Kernel Build  
  - Uncompress using following command at the android directory
        a) tar -xvzf M250_Nougat_Kernel.tar.gz

  - When you compile the kernel source code, you have to add google original prebuilt source(toolchain) into the android directory.
  - Run following scripts to build kernel
  
    a) cd kernel-3.18
    b) mkdir -p out
    c) export PATH=$PATH:$(pwd)/tools/lz4
    d) make -C ../kernel-3.18 O=./out ARCH=arm CROSS_COMPILE=../prebuilts/gcc/linux-x86/arm/cit-arm-eabi-4.8/bin/arm-eabi- ROOTDIR=../android  LGE_TARGET_PLATFORM=mt6750 LGE_TARGET_DEVICE=mlv5  lv5_global_com_debug_defconfig -j4
    e) make -C ../kernel-3.18 O=./out ARCH=arm CROSS_COMPILE=../prebuilts/gcc/linux-x86/arm/cit-arm-eabi-4.8/bin/arm-eabi- ROOTDIR=../android  LGE_TARGET_PLATFORM=mt6750 LGE_TARGET_DEVICE=mlv5  headers_install -j4
    f) make -C ../kernel-3.18 O=./out ARCH=arm CROSS_COMPILE=../prebuilts/gcc/linux-x86/arm/cit-arm-eabi-4.8/bin/arm-eabi- ROOTDIR=../android  LGE_TARGET_PLATFORM=mt6750 LGE_TARGET_DEVICE=mlv5 -j4
	
	* "-j4" : The number, 4, is the number of multiple jobs to be invoked simultaneously. 
  - After build, you can find the build image(Image.gz) at arch/arm/boot
