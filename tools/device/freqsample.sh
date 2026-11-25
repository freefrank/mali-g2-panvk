#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
mkdir -p /storage/emulated/0/Download/panvk-pin
exec >/storage/emulated/0/Download/panvk-pin/fs-access.txt 2>&1
/system/bin/su -c '
D=/sys/class/devfreq/gpufreq_core
o=/storage/emulated/0/Download/panvk-pin/freq.txt
: > $o
i=0; while [ $i -lt 400 ]; do echo "$(date +%s) $(cat $D/cur_freq) $(cat $D/min_freq) $(cat $D/max_freq)" >> $o; sleep 1; i=$((i+1)); done
'
echo su_exit=$?
