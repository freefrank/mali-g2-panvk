#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-takeover
exec >"$out/dm-access.txt" 2>&1
/system/bin/su -c 'dmesg -T 2>/dev/null | grep -v "avc:" | grep -E "Oct 31 08:23:(4[5-9]|5[0-9])|Oct 31 08:17:0[0-3]|Oct 31 08:16:5[5-9]" | grep -vE "servicemanager|PowerDet|xiaomi-keyboard" | tail -150 > /storage/emulated/0/Download/panvk-takeover/dm.txt; dmesg | grep -v "avc:" | wc -l >> /storage/emulated/0/Download/panvk-takeover/dm.txt; dmesg | grep -v "avc:" | head -2 >> /storage/emulated/0/Download/panvk-takeover/dm.txt'
echo su_exit=$?
