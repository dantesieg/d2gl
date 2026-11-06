#!/bin/sh
# Runs the Windows binaries under Wine in Docker. Usage: ./run_wine_tests.sh [build-dir]
set -u
B=$(cd "${1:-build}" && pwd)
run() { # name, image, cmd...
  name=$1; img=$2; shift 2
  docker run --rm --platform linux/amd64 -v "$B":/t -w /t "$img" sh -c "$* >/dev/null 2>/dev/null; echo \$? > rc.txt"
  echo "=== $name: rc=$(cat "$B/rc.txt")"; rm -f "$B/rc.txt"
  [ -f "$B/hdr10_log.txt" ] && mv "$B/hdr10_log.txt" "$B/log_$name.txt"
}
docker run --rm --platform linux/amd64 -v "$B":/t -w /t d2m-wine sh -c 'wine test_color.exe 2>/dev/null' > "$B/log_test_color.txt"; echo "=== test_color: rc=$? $(tail -1 "$B/log_test_color.txt")"
run shader_selftest d2m-wine wine hdr10_proto.exe --shader_selftest
run encode_selftest_nox d2m-wine wine hdr10_proto.exe --encode_selftest
X='timeout 180 xvfb-run -a -s "-screen 0 1280x800x24" wine hdr10_proto.exe'
run encode_selftest d2m-wine-x "$X --encode_selftest"
run default d2m-wine-x "$X --frames=60 --screenshot"; [ -f "$B/shot.ppm" ] && mv "$B/shot.ppm" "$B/shot_wgl.ppm"
run cpu_copy d2m-wine-x "$X --cpu_copy --frames=60 --screenshot"; [ -f "$B/shot.ppm" ] && mv "$B/shot.ppm" "$B/shot_cpu_copy.ppm"
run cpu_copy_sdr d2m-wine-x "$X --cpu_copy --hdr=0 --frames=30"
run resize d2m-wine-x "$X --cpu_copy --resize_test --frames=45"
run resize_wgl d2m-wine-x "$X --resize_test --frames=45"
run loss d2m-wine-x "$X --cpu_copy --simulate_loss --frames=40"
run cycles d2m-wine-x "$X --cpu_copy --cycles=10"
run cycles_wgl d2m-wine-x "$X --cycles=10"
run no_interop_flag d2m-wine-x "$X --no_interop --frames=10"
