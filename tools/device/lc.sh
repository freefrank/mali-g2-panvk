#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-takeover
exec >"$out/lc-access.txt" 2>&1
/system/bin/su -c 'logcat -d -v threadtime | grep -iE "kbase|panvk|MESA|mesa" | tail -80 > /storage/emulated/0/Download/panvk-takeover/lc.txt'
echo su_exit=$?
