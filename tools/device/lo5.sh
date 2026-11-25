#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
out=/storage/emulated/0/Download/panvk-takeover
exec >"$out/lo5-access.txt" 2>&1
/system/bin/su -c '
out=/storage/emulated/0/Download/panvk-takeover
P=io.github.freefrank.lostodyssey
R=/proc/$(pidof $P)/root/data/data/$P/files/gpu_driver
{ ls -laR $R; sha256sum $(find $R -name "*.so"); cat $(find $R -name meta.json) 2>/dev/null | head -20
  echo "## dmesg"; dmesg | grep -iE "mali|kbase|gpu|fault|csf" | tail -40
  echo "## logcat"; logcat -d -t 20000 | grep -E " 0?8:0[2-5]" | grep -iE "lostodyssey|panvk|mali|DEBUG|libc |exit|Abort" | tail -60
} > $out/lo5.txt 2>&1
'
echo su_exit=$?
