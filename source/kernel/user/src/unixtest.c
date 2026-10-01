#include "posix.h"
int main(void){
    printf("USER=%s HOME=%s PATH=%s PWD=%s\n", getenv("USER"), getenv("HOME"), getenv("PATH"), getenv("PWD"));
    setenv("MYVAR","hello",1);
    printf("MYVAR=%s\n", getenv("MYVAR"));
    struct stat st;
    if(stat("/home",&st)==0) printf("/home isdir=%d\n", S_ISDIR(st.st_mode));
    return 0;
}
