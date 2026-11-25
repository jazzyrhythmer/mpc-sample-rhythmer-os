#!/bin/sh
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out=${1:?Choose an empty build output directory}
mkdir -p "$out/module"
out=$(CDPATH= cd -- "$out" && pwd)
cc=${CC:-gcc}
s="$src/port/original/src/dsp"
"$cc" -std=gnu11 -O2 -shared -fPIC -I"$s" "$s"/*.c -Wl,--no-undefined -o "$out/module/dsp.so" -lm -lpthread

"$cc" -std=gnu11 -O2 -fPIC -fvisibility=hidden -DFM1_ISOLATED=1 -I"$src/generated" -I"$src/wrapper" -I"$src/common" -c "$src/common/move_loader.c" -o "$out/loader.o"
"$cc" -std=gnu11 -O2 -fPIC -fvisibility=hidden -DFM1_ISOLATED=1 -I"$src/generated" -I"$src/wrapper" -I"$src/common" -c "$src/generated/vst2_wrap.c" -o "$out/wrapper.o"
"$cc" -shared "$out/loader.o" "$out/wrapper.o" -ldl -lpthread -lm -Wl,--no-undefined -o "$out/rhythmer-fizzik.so"
