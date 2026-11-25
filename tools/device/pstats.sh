#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-pstats
mkdir -p "$out"
exec >"$out/pstats-access.txt" 2>&1
/system/bin/su -c '
export PATH=/system/bin:/system/xbin
out=/storage/emulated/0/Download/panvk-pstats
p=io.github.freefrank.lostodyssey
d=/storage/emulated/0/Android/data/$p/files
{ ls -laR $d/logs 2>&1 | head -40
  find /data/data/$p /data/media/0/Android/data/$p -name "pipeline-stats*" 2>/dev/null
  for f in $(find /data/media/0/Android/data/$p -name "pipeline-stats*" 2>/dev/null); do
    cp "$f" "$out/$(basename $(dirname $f))-$(basename $f)" 2>&1
  done
  ls -la $out
} > $out/pstats.txt 2>&1
'
echo su_exit=$? >> /storage/emulated/0/Download/panvk-pstats/pstats-access.txt
