#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
exec >/storage/emulated/0/Download/panvk-takeover/prio-access.txt 2>&1
/system/bin/su -c "setprop debug.mesa.panvk.kbase.group.priority $(cat /storage/emulated/0/Download/panvk-takeover/prio-value.txt); echo prio=\$(getprop debug.mesa.panvk.kbase.group.priority) inflight=\$(getprop debug.mesa.panvk.kbase.max.inflight)"
echo su_exit=$?
