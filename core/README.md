# core

The kernel core.

| Folder or file | What is in it |
|---|---|
| `init/` | `kmain` (`main.c`), boot menu, splash, setup (OOBE) and the disk installer |
| `proc/` | ELF loader, processes, threads, scheduler, signals and job control |
| `klog.c` | kernel log |
| `panic.c` | panic screen |
| `power.c` | ACPI tables, sleep, reboot and power-off |
| `sound.c`, `media.c` | sound mixer and media helpers |
| `buildsig.c` | build signature checked at boot |
