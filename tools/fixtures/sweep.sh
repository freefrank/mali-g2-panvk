#!/bin/bash
# usage: sweep.sh  -- 1080p, 17 passes per frame, alternating vendor and each variant, 3 rounds
cd /root/mali-g2-project/tools
L=work/lab/libvulkan_panfrost.so
V="build/shaders/tiles.vert.spv build/shaders/tiles.frag.spv"; S=build/shaders/tiles_sample.comp.spv
run() { # name driver extra-env...
  local name=$1 drv=$2; shift 2
  local out
  out=$(env -u LD_PRELOAD LD_LIBRARY_PATH=/vendor/lib64:/vendor/lib64/egl:/system/lib64 \
    MALI_G2_TILES_TIMING=1 MALI_G2_TILES_PASSES=17 "$@" \
    timeout 120 build/bin/vulkan_tiles $drv $S 1920 1080 41 37 $V 2>&1)
  echo "$name $(echo "$out" | grep -oE 'TILES_(PASS|FAIL)|render_median_us=[0-9.]+' | paste -sd' ')"
}
for round in 1 2 3; do
  echo "--- round $round"
  run vendor /vendor/lib64/hw/vulkan.mali.so
  run base $L
  run clr_bit6 $L PANVK_KBASE_MEM_CLR=0x40
  run set_bit23_25 $L PANVK_KBASE_MEM_SET=0x2800000
  run vendorlike $L PANVK_KBASE_MEM_CLR=0x40 PANVK_KBASE_MEM_SET=0x2800000
  run flush_clean_only $L PANVK_KBASE_FLUSH_MODE=1
  run flush_noother $L PANVK_KBASE_FLUSH_MODE=2
done
