#include "posix.h"
int main(void){
    /* /dev/zero: read zeros */
    int z=open("/dev/zero",O_RDONLY); char b[8]; read(z,b,8); close(z);
    int allzero=1; for(int i=0;i<8;i++)if(b[i])allzero=0;
    printf("/dev/zero reads zeros: %s\n", allzero?"yes":"no");
    /* /dev/null: write discards */
    int n=open("/dev/null",O_WRONLY); int w=write(n,"junk",4); close(n);
    printf("/dev/null accepted %d bytes\n", w);
    /* /dev/random: nonzero-ish */
    int r=open("/dev/random",O_RDONLY); unsigned char rb[4]; read(r,(char*)rb,4); close(r);
    printf("/dev/random: %02x%02x%02x%02x\n", rb[0],rb[1],rb[2],rb[3]);
    /* stat */
    struct stat st;
    if(stat("/home",&st)==0) printf("stat /home: isdir=%d mode=%o\n", S_ISDIR(st.st_mode), st.st_mode);
    if(stat("/dev/null",&st)==0) printf("stat /dev/null: ischr=%d\n", S_ISCHR(st.st_mode));
    return 0;
}
