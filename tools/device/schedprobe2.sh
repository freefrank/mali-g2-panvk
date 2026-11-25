#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
exec >/storage/emulated/0/Download/panvk-logs/sched2-access.txt 2>&1
/system/bin/su -c '
o=/storage/emulated/0/Download/panvk-logs/sched2.txt
D=/sys/class/misc/mali0/device
{ echo "== scheduling/"; ls -la $D/scheduling; for f in $D/scheduling/*; do echo "-- $f: $(cat $f 2>&1 | head -5)"; done
  echo "== prioritized"; ls -la $D/add_prioritized_process $D/remove_prioritized_process; cat $D/add_prioritized_process 2>&1 | head
  echo "== firmware_config"; ls $D/firmware_config 2>&1 | head -30
  echo "== sf/sysui pids"; pidof surfaceflinger; pidof com.android.systemui
  echo "== grep kernel symbols"; grep -iE "prioritized_process|kbase_csf_scheduler_prioriti" /proc/kallsyms 2>/dev/null | head
} > $o 2>&1
chmod a+r $o
'
echo su_exit=$?
