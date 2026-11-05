#include <sys/syscall_nums.h>

.section .text
.global __mlibc_restorer
__mlibc_restorer:
	/* RSP = ucontext structure */
	mov %rsp, %rdi
	mov $SYS_SIGRETURN, %rax
	syscall
	ud2
.section .note.GNU-stack,"",%progbits
