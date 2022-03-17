#!/bin/bash
#origin code: KYG-yaya573142/fibdrv/do_measurement.sh
#This script tweak the system setting to minimize the unstable factors while
#analyzing the performance of fibdrv.
#
#2020/3/21  ver 1.0
# targeting for raspberry pi model B
: '
CPUID=3
ORIG_ASLR=`cat /proc/sys/kernel/randomize_va_space`
ORIG_GOV=`cat /sys/devices/system/cpu/cpu$CPUID/cpufreq/scaling_governor`

sudo bash -c "echo 0 > /proc/sys/kernel/randomize_va_space"
sudo bash -c "echo performance > /sys/devices/system/cpu/cpu$CPUID/cpufreq/scaling_governor"

#measure the performance of fibdrv
make client_statistic
# make unload
make load
rm -f plot_input_statistic
sudo taskset -c $CPUID ./client_statistic
gnuplot scripts/plot-statistic.gp
make unload

# restore the original system settings
sudo bash -c "echo $ORIG_ASLR >  /proc/sys/kernel/randomize_va_space"
sudo bash -c "echo $ORIG_GOV > /sys/devices/system/cpu/cpu$CPUID/cpufreq/scaling_governor"
'

#2020/3/21  ver 1.0
# targeting for ubuntu desktop (AMD Ryzen 4650G)

CPUID=11
ORIG_ASLR=`cat /proc/sys/kernel/randomize_va_space`

ORIG_GOV=`cat /sys/devices/system/cpu/cpu$CPUID/cpufreq/scaling_governor`
#ORIG_TURBO=`cat /sys/devices/system/cpu/intel_pstate/no_turbo`  ## disable INTEL turbo mode

sudo bash -c "echo 0 > /proc/sys/kernel/randomize_va_space"
sudo bash -c "echo performance > /sys/devices/system/cpu/cpu$CPUID/cpufreq/scaling_governor"
#sudo bash -c "echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo"  ## disable INTEL turbo mode

#measure the performance of fibdrv
make client_statistic
# make unload
make load
rm -f plot_input_statistic
sudo taskset -c ${CPUID} ./client_statistic
gnuplot scripts/plot-statistic.gp
make unload

# restore the original system settings
sudo bash -c "echo 2 >  /proc/sys/kernel/randomize_va_space"
sudo bash -c "echo powersave > /sys/devices/system/cpu/cpu$CPUID/cpufreq/scaling_governor"
#sudo bash -c "echo 0 > /sys/devices/system/cpu/intel_pstate/no_turbo"  ## disable INTEL turbo mode