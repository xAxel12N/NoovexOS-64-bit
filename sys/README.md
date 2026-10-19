# sys

The system-call layers programs talk to.

| Folder | What is in it |
|---|---|
| `native/` | NoovexOS `int 0x80` system calls (`syscall.c`) and the kernel API for apps and modules (`kapi.c`) |
| `linux/` | the Linux system-call layer that runs musl and glibc programs (`linux.c`) and its sockets (`lxsock.c`) |
