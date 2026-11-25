#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-takeover
exec >"$out/edenlog-access.txt" 2>&1
/system/bin/su -c '
L=/storage/emulated/0/Android/data/com.miHoYo.Yuanshen.nightly/files/log
ls -l $L > /storage/emulated/0/Download/panvk-takeover/edenlog.txt
for f in eden_log.txt eden_log.txt.old.txt; do echo "== $f"; grep -nE "Error|Critical|Vulkan|INITIALIZATION|Exception|panvk|Assert" $L/$f | tail -40; done >> /storage/emulated/0/Download/panvk-takeover/edenlog.txt
'
echo su_exit=$?
