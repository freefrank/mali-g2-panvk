#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-takeover
mkdir -p "$out"
exec >"$out/tomb-access.txt" 2>&1
/system/bin/su -c '
export PATH=/system/bin:/system/xbin
out=/storage/emulated/0/Download/panvk-takeover
ls -lt /data/tombstones/ | grep -v "\.pb" | head -6 > $out/tomb-list.txt
for f in tombstone_09; do
  echo "=== $f"; head -c 200000 /data/tombstones/$f | sed -n "/backtrace:/,/^$/p" | head -40
done > $out/tomb-head.txt
'
echo su_exit=$?
