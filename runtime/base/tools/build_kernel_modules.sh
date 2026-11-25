#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Builds the modules under kernel/<name>/ (each folder's Kbuild names its
# module) against an MPC Sample kernel and leaves each .ko beside its source,
# where it is kept in Git (tools/check_private_files.py lets a module through
# beside the C it is built from) and the build embeds it
# (src/drivers/panel_module.S, src/drivers/kernel_modules.S). Run it after
# changing a module's source, for both kernels, and commit all of them.
#
# KERNEL picks the kernel; each needs its source in .local and the MPC's
# /proc/config.gz from that firmware; the tree is prepared once:
#   6.12 (firmware 1.3.0.12, default): inMusic's public 6.12.60 release in
#        .local/kbuild/kernel-source, .local/mpc-kernel.config; <module>.ko
#   6.18 (firmware 1.4.0.7 and 1.4.0.8): inMusic's public 6.18.45 release (engine-os-g2/5.3)
#        brought to 6.18.59 with kernel.org's stable increments and named
#        6.18.59-imb-2026-10-26-rt9, in .local/kbuild-6.18/kernel-source,
#        .local/mpc-kernel-6.18.config; <module>-6.18.ko
# A folder named <name>-6.18 holds a module's source for 6.18 only (its
# kernel's code moved too far to share one copy): KERNEL=6.18 builds it in
# place of <name>, which then builds for 6.12 only.
# With names, only those folders: KERNEL=6.18 tools/build_kernel_modules.sh uvc vepu
set -eu
cd "$(dirname "$0")/.."
kernel=${KERNEL:-6.12}
case "$kernel" in
6.12) tree=kbuild; config=mpc-kernel.config; suffix= ;;
6.18) tree=kbuild-6.18; config=mpc-kernel-6.18.config; suffix=-6.18 ;;
*) echo "KERNEL is 6.12 or 6.18" >&2; exit 1 ;;
esac
[ -d ".local/$tree/kernel-source" ] && [ -f ".local/$config" ] || {
    echo "Needs .local/$tree/kernel-source and .local/$config" >&2; exit 1; }
names=$*
if [ -z "$names" ]; then
    for kbuild in kernel/*/Kbuild; do
        name=$(basename "$(dirname "$kbuild")")
        [ "${name%-6.18}" = "$name" ] && names="$names $name"
    done
fi
docker build -q --platform linux/arm64 -t sample-base-kbuild tools/kbuild >/dev/null
for name in $names; do
    name=${name%-6.18}
    [ "$kernel" = 6.18 ] && [ -f "kernel/$name-6.18/Kbuild" ] && name=$name-6.18
    [ -f "kernel/$name/Kbuild" ] || { echo "kernel/$name has no Kbuild" >&2; exit 1; }
    out="build/kernel-$name$suffix"
    rm -rf "$out" && mkdir -p "$out"
    cp "kernel/$name"/Kbuild "kernel/$name"/*.c "$out/"
    for header in "kernel/$name"/*.h; do [ -f "$header" ] && cp "$header" "$out/"; done
    docker run --rm --platform linux/arm64 -v "$PWD/.local:/k" -v "$PWD/$out:/m" \
        -e TREE="$tree" -e CONFIG="$config" sample-base-kbuild sh -c '
        set -eu
        cd "/k/$TREE/kernel-source"
        [ -f include/config/kernel.release ] || { cp "/k/$CONFIG" .config; make -s olddefconfig; make -s -j8 modules_prepare; }
        # The prepared tree has no Module.symvers: modpost cannot see the
        # kernel'"'"'s symbols, which resolve when the module loads.
        make -s -j8 M=/m KBUILD_MODPOST_WARN=1 modules 2>&1 | grep -v -e "undefined!" -e "Module.symvers" -e "modversions" \
            -e "unresolved symbol errors" -e "KBUILD_MODPOST_WARN" -e "own risk" -e "suppressed" || true
        for ko in /m/*.ko; do
            strip --strip-debug "$ko"
            modinfo "$ko" | grep -E "^(name|vermagic|parm):"
        done'
    for ko in "$out"/*.ko; do
        target="kernel/$name/$(basename "$ko" .ko)$suffix.ko"
        cp "$ko" "$target"
        echo "$target"
    done
done
