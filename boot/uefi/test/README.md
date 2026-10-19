UEFI boot tests (run on Linux, no firmware needed).

- `pecheck.py BOOTX64.EFI` checks the PE file against the structural rules UEFI PE loaders enforce.
- `isocheck.py IMAGE.iso BOOTX64.EFI kernel64.bin` follows the firmware's CD (El Torito EFI entry) and USB (MBR ESP) boot paths through an image, classifying FAT by cluster count and comparing files byte for byte. Needs `fatck.py` and `pecheck.py` beside it.
- `firmware_sim.c` runs the loader's C code against simulated UEFI services (compile `uefiboot.c` with `-DUEFI_TEST`).
- `pe_run.c` loads the built `BOOTX64.EFI` like firmware does, runs it with the simulated services, and checks it reaches the kernel entry. Build with `gcc -O1 -D_GNU_SOURCE -fshort-wchar -o pe_run pe_run.c`, run from a directory containing `kernel64.bin`.
