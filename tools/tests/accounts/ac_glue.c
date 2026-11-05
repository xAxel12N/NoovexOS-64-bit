#include "fs_glue.c"
typedef void (*ac_rep_fn)(const char*,int,const char*);
static ac_rep_fn ac_rep;
static int ac_has(const char* path,const char* needle){
    static char b[24576]; int isd=0; int k=ux_lookup(path,&isd,1); if(k<0||isd)return 0;
    int n=FS[k].len; if(n>(int)sizeof(b)-1)n=(int)sizeof(b)-1; const char* s=fsd(k); for(int i=0;i<n;i++)b[i]=s[i]; b[n]=0;
    int nl=0; while(needle[nl])nl++;
    for(int i=0;i+nl<=n;i++){ int m=1; for(int j=0;j<nl;j++) if(b[i+j]!=needle[j]){ m=0; break; } if(m)return 1; }
    return 0;
}
static unsigned ac_dmode(const char* p){ int d=dt_resolve_dir(p); return d<0?0:DT[d].mode; }
static int ac_duid(const char* p){ int d=dt_resolve_dir(p); return d<0?-1:DT[d].uid; }
static void ac_put(const char* path,const char* body){ int n=0; while(body[n])n++; ux_write_text(path,body,n,0); }
int act_run(ac_rep_fn rep){
    ac_rep=rep;
    fk_heap(); fk_boot(1); g_uid=0; g_gid=0;
    ux_core_start(0);
    rep("boot creates root, the first account and guest",ac_has("/etc/passwd","root:x:0:0:")&&ac_has("/etc/passwd",":x:1000:1000:")&&ac_has("/etc/passwd","guest:x:999:999:"),0);
    rep("groups admin, sudo and users exist",ac_has("/etc/group","admin:x:10:")&&ac_has("/etc/group","sudo:x:27:")&&ac_has("/etc/group","users:x:100:"),0);
    rep("root is locked until setup sets a password",ac_has("/etc/shadow","root:!:"),0);
    rep("standard folders and modes (/root 0700, /var/tmp 1777, /usr/local/bin, /media, /run)",(ac_dmode("/root")&07777)==0700&&(ac_dmode("/var/tmp")&07777)==01777&&dt_resolve_dir("/usr/local/bin")>=0&&dt_resolve_dir("/media")>=0&&dt_resolve_dir("/run")>=0,0);
    rep("/etc/os-release, hosts, shells, sudoers, login.defs",ac_has("/etc/os-release","ID=noovexos")&&ac_has("/etc/hosts","127.0.0.1")&&ac_has("/etc/shells","/bin/nsh")&&ac_has("/etc/sudoers","timestamp_timeout=15")&&ac_has("/etc/login.defs","ENCRYPT_METHOD SHA512"),0);
    { int isd=0; int k=ux_lookup("/etc/shadow",&isd,1); rep("/etc/shadow is 0600 root",k>=0&&(FS[k].mode&07777)==0600&&FS[k].uid==0,0); }
    { int h=dt_resolve_dir("/home"); int k=-1; for(int i=0;i<FS_N;i++) if(!FS[i].used){ k=i; break; } fs_newslot(k); FS[k].name[0]='n'; FS[k].name[1]=0; fs_set(k,"x",1); FS[k].used=1; FS[k].dir=h; }
    int r=acct_setup_primary("harald","Harald von Koch","pw1234","rootpw","laptop");
    rep("setup creates the administrator",r==0&&ac_has("/etc/passwd","harald:x:1000:1000:Harald von Koch:/home/harald:/bin/nsh"),g_acct_err);
    rep("personal group, admin, sudo and users membership",ac_has("/etc/group","harald:x:1000:")&&ac_has("/etc/group","admin:x:10:harald")&&ac_has("/etc/group","sudo:x:27:harald")&&ac_has("/etc/group","users:x:100:harald"),0);
    rep("passwords are stored as SHA-512 crypt",ac_has("/etc/shadow","harald:$6$")&&ac_has("/etc/shadow","root:$6$"),0);
    rep("home folder with Desktop..Videos owned by the user, mode 0750",(ac_dmode("/home/harald")&07777)==0750&&ac_duid("/home/harald")==1000&&ac_duid("/home/harald/Documents")==1000&&dt_resolve_dir("/home/harald/Videos")>=0,0);
    { int k=k_find_file("/home/harald/n"); rep("files that were in /home move into the new home",k>=0&&FS[k].uid==1000,0); }
    rep("hostname and /etc/hosts",ac_has("/etc/hostname","laptop")&&ac_has("/etc/hosts","127.0.1.1\tlaptop"),0);
    rep("right password accepted, wrong refused, root password works",acct_check("harald","pw1234")==1&&acct_check("harald","pw1235")==0&&acct_check("harald","")==0&&acct_check("root","rootpw")==1&&acct_check("nobody","x")==0,0);
    int u=acct_create("anna","Anna","abcd",0);
    rep("second account gets uid 1001, standard, home 0750",u==1001&&!acct_is_admin(1001)&&acct_is_admin(1000)&&(ac_dmode("/home/anna")&07777)==0750&&ac_duid("/home/anna")==1001,g_acct_err);
    rep("bad names and short passwords are refused",acct_create("Bad Name","x","abcd",0)<0&&acct_create("root","x","abcd",0)<0&&acct_create("anna","x","abcd",0)<0&&acct_create("bob","x","abc",0)<0,0);
    rep("make admin, and the last administrator cannot be removed",acct_set_admin("anna",1)==0&&acct_is_admin(1001)&&acct_set_admin("harald",0)==0&&acct_set_admin("anna",0)<0&&acct_delete("anna",1)<0,0);
    acct_set_admin("harald",1);
    rep("password change and lock/unlock",acct_set_password("anna","newpass")==0&&acct_check("anna","newpass")&&!acct_check("anna","abcd")&&acct_set_locked("anna",1)==0&&!acct_check("anna","newpass")&&acct_set_locked("anna",0)==0&&acct_check("anna","newpass"),g_acct_err);
    rep("delete with home folder",acct_delete("anna",1)==0&&!ac_has("/etc/passwd","anna:")&&!ac_has("/etc/shadow","anna:")&&!ac_has("/etc/group","anna")&&dt_resolve_dir("/home/anna")<0,g_acct_err);
    { char gl[200]; acct_groups("harald",gl,200); rep("groups lists the personal group first",gl[0]=='h'&&gl[1]=='a',gl); }
    acct_login("harald");
    rep("login sets uid, gid, home and the working folder",g_uid==1000&&g_gid==1000&&seq(g_home,"/home/harald")&&g_home_dir==dt_resolve_dir("/home/harald")&&g_dcwd==g_home_dir,g_home);
    { int hd=dt_resolve_dir("/home/harald"); int sv=g_uid, sg=g_gid; g_uid=1001; g_gid=1001; int other=perm_dir(hd); g_uid=sv; g_gid=sg; int own=perm_dir(hd);
      int etc=dt_resolve_dir("/etc"); int sysw=perm_dir(etc);
      rep("access: another user cannot write in your home, you can, admins cannot write /etc without sudo",!other&&own&&!sysw,0); }
    acct_logout(); g_uid=0; g_gid=0;
    { static char lb[2048]; acct_last(lb,2048); int hasl=0, hasb=0; for(int i=0;lb[i];i++){ if(lb[i]=='h'&&lb[i+1]=='a'&&lb[i+2]=='r')hasl=1; if(lb[i]=='r'&&lb[i+1]=='e'&&lb[i+2]=='b')hasb=1; }
      rep("last shows the session and the boot",hasl&&hasb,lb); }
    rep("auth.log records logins and changes",ac_has("/var/log/auth.log","session opened for harald")&&ac_has("/var/log/auth.log","user removed: anna"),0);
    ac_put("/etc/passwd","root:x:0:0:root:/root:/bin/nsh\nKalle:x:1000:1000:Kalle:/home:/bin/nsh\n");
    ac_put("/etc/group","root:x:0:\nadmin:x:10:Kalle\nusers:x:1000:Kalle\n");
    { unsigned h=nvx_pwhash("secret"); char t[16]; int n=0; while(h){ t[n++]="0123456789abcdef"[h&15]; h>>=4; } char b[64]; char* p=b; p=scopy(p,"Kalle:"); while(n)*p++=t[--n]; *p++='\n'; *p=0; ac_put("/etc/shadow",b); }
    acct_boot();
    rep("old installs: group 1000 becomes the personal group, users and sudo are added",ac_has("/etc/group","Kalle:x:1000:")&&ac_has("/etc/group","users:x:100:Kalle")&&ac_has("/etc/group","sudo:x:27:Kalle"),0);
    rep("old installs: the old password still works and is upgraded to SHA-512",acct_check("Kalle","secret")==1&&ac_has("/etc/shadow","Kalle:$6$")&&acct_check("Kalle","secret")==1&&acct_check("Kalle","wrong")==0,0);
    ac_put("/etc/shadow","Kalle:*\n"); acct_boot();
    rep("old installs without a password sign in with an empty password",acct_check("Kalle","")==1&&!acct_has_password("Kalle"),0);
    { acct_info a[8]; g_guest_on=0; int n0=acct_list(a,8,1); g_guest_on=1; int n1=acct_list(a,8,1); g_guest_on=0; rep("guest is listed only when turned on; root is never listed",n1==n0+1&&n0>=1,0); }
    acct_create("zoe","Zoe","zoepass",0); fk_sync(); fk_boot(0); g_uid=0; g_gid=0;
    rep("accounts and passwords survive a restart (written to the disk)",disk_ok&&ac_has("/etc/passwd","zoe:x:")&&acct_check("zoe","zoepass")==1&&dt_resolve_dir("/home/zoe/Documents")>=0,0);
    { char o[160]; int n=sha512_crypt("Hello","$6$saltstring",o,160); rep("SHA-512 crypt matches the reference",n>0&&seq(o,"$6$saltstring$aQzKv7HhksN4CNT5HySRdxOEHxZvlWWP2je/lOgbrHx5iLYj3NJfVnC287n/dwkODYWL1.LZUdO9vX84fkCna/"),o); }
    return 0;
}
