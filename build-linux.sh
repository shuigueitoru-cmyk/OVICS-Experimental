#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
mode=${1:-}
extra=(-Ihorizon/include -include horizon/native_limits.h)
if [[ $mode == --timer-rollover-test ]]; then extra+=("-DOVICS_TIMER_ROLLOVER_TEST=1"); fi
if [[ $mode == --timer-100 ]]; then extra+=("-DOVICS_TIMER_HZ=100"); fi
if [[ $mode == --timer-1000 ]]; then extra+=("-DOVICS_TIMER_HZ=1000"); fi
object_dir=build/normal
output=ovics.elf
if [[ $mode == --timer-rollover-test ]]; then object_dir=build/timer-rollover; output="$object_dir/ovics.elf"; fi
if [[ $mode == --timer-100 ]]; then object_dir=build/timer-100; output="$object_dir/ovics.elf"; fi
if [[ $mode == --timer-1000 ]]; then object_dir=build/timer-1000; output="$object_dir/ovics.elf"; fi
if [[ $mode == --fault-test ]]; then
 case ${2:-} in
  read) fault=1;; readonly) fault=2;; write) fault=3;; null) fault=4;; kernel) fault=5;;
  *) echo 'Use --fault-test read|readonly|write|null|kernel' >&2; exit 2;;
 esac
 extra+=("-DOVICS_VM_FAULT_TEST=$fault")
 object_dir="build/fault-${2}"
 output="$object_dir/ovics.elf"
fi
if [[ $mode == --graphics-fault-test ]]; then
 extra+=("-DOVICS_GFX_FAULT_TEST=1")
 object_dir=build/gfx-fault
 output="$object_dir/ovics.elf"
fi
if [[ $mode == --window-api-test ]]; then
 extra+=("-DOVICS_WM_API_TEST=1")
 object_dir=build/window-api
 output="$object_dir/ovics.elf"
fi
mkdir -p "$object_dir"
rm -f "$object_dir/boot.o" "$object_dir/kernel.o" "$output"
nasm -f elf32 boot.asm -o "$object_dir/boot.o"
if [[ ${CC:-gcc} == gcc ]]; then
 gcc -m32 -fno-pie -Wno-misleading-indentation -ffreestanding -fno-stack-protector -Wall -Wextra -Werror -fstack-usage "${extra[@]}" -c kernel.c -o "$object_dir/kernel.o"
else
 "${CC}" --target=i686-elf -ffreestanding -fno-stack-protector -Wall -Wextra -Werror -fstack-usage "${extra[@]}" -c kernel.c -o "$object_dir/kernel.o"
fi
for module in html layout; do
 gcc -m32 -fno-pie -ffreestanding -fno-stack-protector -Wall -Wextra -Werror -Wno-misleading-indentation "${extra[@]}" -c "horizon/src/$module.c" -o "$object_dir/horizon-$module.o"
done
${LD:-ld} -m elf_i386 -T linker.ld -Map="$object_dir/memory-map.txt" "$object_dir/boot.o" "$object_dir/kernel.o" "$object_dir/horizon-html.o" "$object_dir/horizon-layout.o" -o "$output"
if [[ $mode == --run ]]; then
 qemu-system-i386 -vga std -m 128 -kernel "$output" -netdev user,id=net,net=10.0.2.0/24,host=10.0.2.2,dns=10.0.2.3 -device rtl8139,netdev=net
fi
