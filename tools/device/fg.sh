#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
exec >/storage/emulated/0/Download/panvk-takeover/fg-access.txt 2>&1
/system/bin/su -c 'dumpsys activity activities | grep topResumedActivity'
echo su_exit=$?
