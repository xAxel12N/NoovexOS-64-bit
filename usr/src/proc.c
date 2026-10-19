#include "user.h"
int main(void){
    sys_write("Hello from a child process!\n");
    sys_write("I was spawned via fork+exec, and I'm exiting with status 42.\n");
    _exit(42);
    return 0;
}
