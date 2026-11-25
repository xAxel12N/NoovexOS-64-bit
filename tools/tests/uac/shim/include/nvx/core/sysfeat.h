struct featguard { const char* name; const char* flag; int on, fails, maxfail, dead, trying; };
void fg_boot(struct featguard* g); int fg_begin(struct featguard* g); void fg_ok(struct featguard* g); void fg_fail(struct featguard* g,const char* why);
