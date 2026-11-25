#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
exec >/storage/emulated/0/Download/panvk-takeover/install.txt 2>&1
/system/bin/su -c 'P=io.github.freefrank.lostodyssey; dumpsys package $P | grep -E "versionName|versionCode|lastUpdateTime" | head -3; cp /storage/emulated/0/Download/LostOdysseyRecomp-diag-pipeline-stats-c9c299e.apk /data/local/tmp/lor-diag.apk; chmod 644 /data/local/tmp/lor-diag.apk; S=$(stat -c %s /data/local/tmp/lor-diag.apk); ID=$(pm install-create -r -d -S $S | grep -o "[0-9]*"); echo session=$ID; cat /data/local/tmp/lor-diag.apk | pm install-write -S $S $ID base.apk -; pm install-commit $ID; rm -f /data/local/tmp/lor-diag.apk; dumpsys package $P | grep -E "versionName|versionCode|lastUpdateTime" | head -3; ls /data/data/$P/files/save | head -3'
echo su_exit=$?
