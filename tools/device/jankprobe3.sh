#!/system/bin/sh
export PATH=/system/bin:/system/xbin TERMUX_EXEC_OPTOUT=1
unset LD_PRELOAD LD_LIBRARY_PATH
exec >/storage/emulated/0/Download/panvk-jank/probe3-access.txt 2>&1
/system/bin/su -c '
o=/storage/emulated/0/Download/panvk-jank/probe3.txt
s=$(date +%s.%N)
for t in 1 2 3 4 5; do dumpsys SurfaceFlinger --frametimeline -jank </dev/null 2>/dev/null | grep -A2 "^Display Frame" | grep "Jank Type" | sed "s/^/$t /"; done > $o
e=$(date +%s.%N)
echo "elapsed $s $e" >> $o
chmod a+r $o
'
echo su_exit=$?
