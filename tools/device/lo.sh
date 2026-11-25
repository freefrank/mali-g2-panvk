#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-takeover
mkdir -p "$out"
exec >"$out/lo-access.txt" 2>&1
/system/bin/su -c '
export PATH=/system/bin:/system/xbin
out=/storage/emulated/0/Download/panvk-takeover
{ pm list packages | grep -iE "lostodyssey"
  for p in $(pm list packages | grep -iE "lostodyssey" | cut -d: -f2); do
    echo "== $p"; dumpsys package $p | grep -E "versionName|lastUpdateTime|dataDir|nativeLibraryDir" | head
    ls -la /data/data/$p /data/data/$p/files 2>&1 | head -40
    ls -la /storage/emulated/0/Android/data/$p/files 2>&1 | head -30
    dumpsys activity exit-info $p | grep -A4 "ApplicationExitInfo #0" 
  done
  ls -lt /data/tombstones | head -4; logcat -d -t 3000 | grep -iE "lostodyssey|panvk|vulkan|DEBUG  " | tail -80
} > $out/lo.txt 2>&1
'
echo su_exit=$?
