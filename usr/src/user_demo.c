#include "user.h"
int main(void){
    sys_write("[task] start\n");  yield();
    sys_write("[task] middle\n"); yield();
    sys_write("[task] done\n");
    return 0;
}
