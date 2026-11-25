#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
exec >/storage/emulated/0/Download/panvk-takeover/cleanapp.txt 2>&1
/system/bin/su -mm -c '
P=io.github.freefrank.lostodyssey; E=com.miHoYo.Yuanshen.nightly
EL=/storage/emulated/0/Android/data/$E/files/log
echo "## running"; pidof $P $E
echo "## LOR captures"; du -sh /data/data/$P/files/captures; rm -rf /data/data/$P/files/captures/*
echo "## LOR drivers"; ls /data/data/$P/files/gpu_driver
for d in /data/data/$P/files/gpu_driver/panvk-mali-g2-test*; do case "$d" in *test62*) ;; *) rm -rf "$d";; esac; done
ls /data/data/$P/files/gpu_driver
echo "## Eden log dir"; ls -la $EL
rm -f $EL/eden_gpu.log $EL/eden_gpu.log.old.txt
echo "## Eden gpu_drivers"; ls /storage/emulated/0/Android/data/$E/files/gpu_drivers
echo "## Eden dump"; ls /storage/emulated/0/Android/data/$E/files/dump | head
echo "## tombstones"; ls -la /data/tombstones | head -4; rm -f /data/tombstones/tombstone_*
du -sh /data/tombstones
echo "## props"; for p in debug.mesa.panvk.g2.diagnostics debug.mesa.panvk.debug debug.mesa.panvk.kbase.max.inflight debug.mesa.panvk.g2.tib.size debug.mesa.panvk.kbase.group.priority; do echo "$p=$(getprop $p)"; done
'
echo su_exit=$?
