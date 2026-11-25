#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
exec >/storage/emulated/0/Download/panvk-pin/klog-access.txt 2>&1
/system/bin/su -c '
o=/storage/emulated/0/Download/panvk-pin/klog.txt
D=/sys/class/devfreq/gpufreq_core
{ date; echo "governors: $(cat $D/available_governors)"; ls $D; dmesg | grep -v "avc:" | grep -iE "mali|kbase|gpu|csf|fault" | tail -40; echo "--- polling_interval=$(cat $D/polling_interval) boost=$(cat $D/boost_enable) target=$(cat $D/target_freq)"; } > $o 2>&1
'
echo su_exit=$?
