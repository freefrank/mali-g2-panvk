#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
exec >/storage/emulated/0/Download/panvk-takeover/getcap.txt 2>&1
/system/bin/su -c 'P=io.github.freefrank.lostodyssey; C=/proc/$(pidof $P)/root/data/data/$P/files/captures; ls -la $C; rm -rf /storage/emulated/0/Download/panvk-cap; mkdir -p /storage/emulated/0/Download/panvk-cap; cp -r $C/* /storage/emulated/0/Download/panvk-cap/; chmod -R a+rX /storage/emulated/0/Download/panvk-cap; du -sh /storage/emulated/0/Download/panvk-cap'
echo su_exit=$?
