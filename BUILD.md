# Build and reproduce

Linux x86: NASM, GCC freestanding-m32, GNU ld elf_i386, QEMU system-x86。
テストにPython3/Pillow。PC中継はPython3標準ライブラリーだけで動作します。

```sh
bash build-linux.sh
qemu-system-i386 -m 8 -vga std -kernel ovics.elf -nic user,model=rtl8139
```

32/128/512MiBは-mを変更。通常版ELFを同梱します。
`bash build-linux.sh --run` は128MiBで起動します。Clang経路は未検証で、Horizonオブジェクトは
GCCを使います。GRUB ISO/UEFI/実機用ブートイメージではありません。

Horizonコアを別translation unitとしてfreestandingでビルドし、kernelへリンクします。
`horizon/native_limits.h`で8192byte入力/16KiBテキスト/512ノード/32リンク/72列×256行へ制限。
実際の描画コールバックを既存Window Managerに接続し、画面へ一度だけ上書きする方式ではありません。

```sh
python3 tests/verify_horizon_native.py  # 実Wikipedia HTTPSをPC中継経由で取得
python3 tests/check_horizon_wire.py    # native HTTP PCAPから実GET/本文/終了を照合
python3 tests/verify_horizon_cancel.py # 遅延HTTP中の実PS2操作とEsc中断
python3 tests/verify_four_apps.py
python3 tests/verify_desktop_v020.py
python3 tests/verify_timer_input_v021.py
python3 tests/verify_windows_v015.py
python3 tests/verify_mouse_v014.py
python3 tests/verify_graphics_v013.py
python3 tests/run_dns_matrix_v012.py
bash tests/run_memory_unit.sh
python3 tests/run_regression.py
bash build-linux.sh --graphics-fault-test
for mode in read readonly write null kernel; do bash build-linux.sh --fault-test "$mode"; done
python3 tests/verify_page_fault_v012.py
python3 tests/verify_graphics_fault_v013.py
python3 tests/verify_cursor_fault_v014.py
python3 tests/verify_window_fault_v015.py
python3 tests/verify_desktop_fault_v020.py
```

ダブルクリック/時間依存試験はQEMUを1台だけ動かして行います。0.2.1から継続する
PIT100Hzの負荷依存制限を、並行VMのスケジューリングと混同しないためです。
受信テストは待受け開始を確認してから注入し、再起動テストは実起動/空RAM FSを確認してから入力。
パケットの判定条件や不正入力拒否条件は維持しています。

コア試験：

```sh
gcc -std=c11 -Ihorizon/include horizon/src/html.c horizon/src/layout.c horizon/tests/upstream_test_core.c -o build/test-upstream
./build/test-upstream
gcc -std=c11 -Ihorizon/include -include horizon/native_limits.h horizon/src/html.c horizon/src/layout.c horizon/tests/native_test_core.c -o build/test-native
./build/test-native
```

同じnative試験に-fsanitize=address,undefinedを付けた検査も行いました。
ASAN_OPTIONS=detect_leaks=0を使用し、LeakSanitizerの成功は主張しません。
