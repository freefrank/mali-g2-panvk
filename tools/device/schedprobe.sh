#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
exec >/storage/emulated/0/Download/panvk-logs/sched-access.txt 2>&1
/system/bin/su -c '
o=/storage/emulated/0/Download/panvk-logs/sched.txt
D=/sys/class/misc/mali0/device
{ ls $D | tr "\n" " "; echo; for f in csg_scheduling_period progress_timeout idle_hysteresis_time scheduling_period; do [ -e $D/$f ] && echo "$f=$(cat $D/$f)"; done;
  ls /sys/kernel/debug/mali0 2>/dev/null | tr "\n" " "; } > $o 2>&1
chmod a+r $o
'
echo su_exit=$?
