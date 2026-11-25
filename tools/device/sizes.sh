#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
exec >/storage/emulated/0/Download/panvk-takeover/sizes.txt 2>&1
/system/bin/su -mm -c '
P=io.github.freefrank.lostodyssey; E=com.miHoYo.Yuanshen.nightly
echo "## LOR private"; du -sh /data/data/$P/files/* 2>/dev/null; ls /data/data/$P/files/gpu_driver; ls /data/data/$P/files/captures
echo "## LOR ext"; du -sh /storage/emulated/0/Android/data/$P/files/* 2>/dev/null
echo "## Eden private"; du -sh /data/data/$E/files/* 2>/dev/null; ls /data/data/$E/files/panvk-backups 2>/dev/null
echo "## Eden ext"; du -sh /storage/emulated/0/Android/data/$E/files/* 2>/dev/null; ls /storage/emulated/0/Android/data/$E/files/gpu_drivers 2>/dev/null | head -60
echo "## tombstones"; du -sh /data/tombstones
echo "## driver selections"; grep "^driver_path=" /storage/emulated/0/Android/data/$E/files/config/config.ini; grep -i driver /data/data/$P/files/config/*.ini 2>/dev/null | head
'
echo su_exit=$?
