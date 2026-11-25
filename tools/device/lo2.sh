#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-takeover
exec >"$out/lo2-access.txt" 2>&1
/system/bin/su -c '
out=/storage/emulated/0/Download/panvk-takeover
{ echo "## pm all users"; pm list packages --user all 2>&1 | grep -iE "freefrank|odyss|lor|recomp" 
  echo "## packages.list"; grep -iE "freefrank|odyss|lor\b|recomp" /data/system/packages.list
  echo "## data dirs"; ls -d /data/data/*freefrank* /data/data/*odyss* /data/user/*/*freefrank* 2>&1
  echo "## ps"; ps -A -o PID,NAME | grep -iE "freefrank|odyss|lor|recomp"
  echo "## pm count"; pm list packages | wc -l
} > $out/lo2.txt 2>&1
'
echo su_exit=$?
