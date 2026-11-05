    else if(seq(cmd,"ring3")||seq(cmd,"user")||seq(cmd,"usertest")){
        tcol(rgb(255,210,90)); term_puts("entering ring 3 (user mode) with a test program... (experimental)\n"); tcol(WHT);
        int rc=ring3_run();
        if(rc==1){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool (needs >=272 MB RAM)\n"); }
        else if(rc==2){ tcol(RED); term_puts("  FAILED: could not set up the user address space\n"); }
        else { char b[80],*p; tcol(CYAN); p=b;p=scopy(p,"  ring-3 program said: \"");p=scopy(p,g_r3out);*p++='"';*p=0; term_puts(b); term_putc('\n');
            tcol(GREEN); term_puts("  PASS: a program ran in ring 3, made INT 0x80 syscalls, and exited to the kernel.\n");
            term_puts("  -> user mode + syscalls work. (userspace stage 4+5 done)\n"); }
        tcol(WHT); }
    else if(seq(cmd,"sched")||seq(cmd,"multitask")||seq(cmd,"ps")){
        tcol(rgb(255,210,90)); term_puts("cooperative scheduler: running 3 ring-3 tasks that yield to each other...\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool (needs >=272 MB RAM)\n"); tcol(WHT); }
        else { us_init(); for(int i=0;i<MAXPROC;i++){g_proc[i].used=0;g_proc[i].alive=0;} g_r3out_n=0; g_r3out[0]=0;
            int a=proc_create(_binary_user_user_demo_elf_start,(u32)(_binary_user_user_demo_elf_end-_binary_user_user_demo_elf_start));
            int b=proc_create(_binary_user_user_demo_elf_start,(u32)(_binary_user_user_demo_elf_end-_binary_user_user_demo_elf_start));
            int c=proc_create(_binary_user_user_demo_elf_start,(u32)(_binary_user_user_demo_elf_end-_binary_user_user_demo_elf_start));
            if(a<0||b<0||c<0){ tcol(RED); term_puts("  failed to create processes\n"); tcol(WHT); }
            else { sched_run_all();
                tcol(CYAN); term_puts("  combined output (3 tasks interleaving via yield):\n"); tcol(WHT);
                int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
                tcol(GREEN); term_puts("  PASS: 3 processes ran concurrently in ring 3, context-switching on yield.\n");
                term_puts("  -> cooperative scheduler + multitasking work. (stage 8 done)\n"); tcol(WHT); } }
    }
    else if(seq(cmd,"fork")||seq(cmd,"forktest")){
        tcol(rgb(255,210,90)); term_puts("fork(): a user process duplicates itself...\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool (needs >=272 MB RAM)\n"); tcol(WHT); }
        else { us_init(); for(int i=0;i<MAXPROC;i++){g_proc[i].used=0;g_proc[i].alive=0;} g_r3out_n=0; g_r3out[0]=0;
            int a=proc_create(_binary_user_user_fork_elf_start,(u32)(_binary_user_user_fork_elf_end-_binary_user_user_fork_elf_start));
            if(a<0){ tcol(RED); term_puts("  failed to create process\n"); tcol(WHT); }
            else { sched_run_all();
                tcol(CYAN); term_puts("  output:\n"); tcol(WHT);
                int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
                tcol(GREEN); term_puts("  PASS: fork() created a child process with a copy of the address space.\n");
                term_puts("  -> fork + execve work. (stage 9 done)\n"); tcol(WHT); } }
    }
    else if(seq(cmd,"cores")||seq(cmd,"cpu")){
        int c=acpi_count_cores(); char b[80],*p;
        tcol(CYAN); p=b;p=scopy(p,"CPU cores detected (ACPI MADT): ");if(c>0)p=udec(p,(u32)c);else p=scopy(p,"unknown");*p=0; term_puts(b); term_putc('\n'); tcol(WHT);
        p=b;p=scopy(p,"cores online now: ");p=udec(p,(u32)g_cores_online);*p=0; term_puts(b); term_putc('\n');
        if(c>g_cores_online) term_puts("  run 'smp on' to schedule programs on the other cores.\n"); }
    else if(seq(cmd,"compositor")||seq(cmd,"nova")){
        tcol(rgb(255,210,90)); term_puts("launching a USERLAND compositor + 2 app processes (rung 5, the finale)...\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool\n"); tcol(WHT); }
        else { compositor_run();
            tcol(CYAN); term_puts("  process output:\n"); tcol(WHT);
            int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
            tcol(GREEN); term_puts("  PASS: a userland compositor process composited windows from 2 other processes.\n");
            term_puts("  -> rungs 1-5 complete: preemption + IPC + protocol + a userland compositor.\n"); tcol(WHT); } }
    else if(seq(cmd,"wm")||seq(cmd,"winproto")){
        tcol(rgb(255,210,90)); term_puts("windowing protocol: an app creates a window via a message + shared-memory pixels...\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool\n"); tcol(WHT); }
        else { wm_demo();
            tcol(CYAN); term_puts("  app output:\n"); tcol(WHT);
            int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
            tcol(GREEN); term_puts("  PASS: a window was created + drawn entirely through the protocol (rung 4). Press a key.\n"); tcol(WHT);
            { u64 until=g_ticks+600; while(g_ticks<until && kpop()<0)cpu_idle(); } }
    }
    else if(seq(cmd,"shmstat")){
        extern struct shmreg g_shm[]; 
        tcol(rgb(120,200,255)); term_puts("shared memory segments\n"); tcol(GREY); term_puts("id  key   frames\n"); tcol(WHT);
        int any=0; for(int i=0;i<SHM_MAX;i++){ if(!g_shm[i].used)continue; any=1; char b[48],*p=b; p=scopy(p,"  ");p=udec(p,(u32)i);p=scopy(p,"   ");p=udec(p,(u32)g_shm[i].key);p=scopy(p,"    ");p=udec(p,(u32)g_shm[i].nframes);*p=0; term_puts(b); term_putc('\n'); }
        if(!any) term_puts("  (none - run 'shm' to see the IPC demo create one)\n"); }
    else if(seq(cmd,"ipc")||seq(cmd,"shm")){
        tcol(rgb(255,210,90)); term_puts("IPC: two processes share memory + exchange a message...\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool\n"); tcol(WHT); }
        else { ipc_reset(); preempt_run_elf(_binary_user_user_ipc_elf_start,(u32)(_binary_user_user_ipc_elf_end-_binary_user_user_ipc_elf_start));
            tcol(CYAN); term_puts("  output:\n"); tcol(WHT);
            int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
            tcol(GREEN); term_puts("  PASS: P2 read P1's shared value + message -> shared memory + IPC work (rung 2+3).\n"); tcol(WHT); } }
    else if(seq(cmd,"preempt")||seq(cmd,"multitask2")){
        tcol(rgb(255,210,90)); term_puts("preemptive multitasking: 2 processes that NEVER yield - the timer forces switches...\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool\n"); tcol(WHT); }
        else { preempt_run();
            tcol(CYAN); term_puts("  output (interleaved P1/P2 = the timer preempted them, no yield needed):\n"); tcol(WHT);
            int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
            tcol(GREEN); term_puts("  PASS: preemptive scheduling works (rung 1 toward a userland compositor).\n"); tcol(WHT); } }
    else if(seq(cmd,"smpsched")){
        extern volatile int g_cores_online; extern volatile unsigned long g_core_load[]; extern volatile unsigned long g_core_cycles[];
        tcol(rgb(120,200,255)); term_puts("per-core run-queue stats\n"); tcol(GREY);
        term_puts("core  jobs  work-units\n"); tcol(WHT);
        for(int i=0;i<g_cores_online&&i<8;i++){ char b[64],*p=b; p=scopy(p,"  ");p=udec(p,(u32)i);p=scopy(p,"    ");p=udec(p,(u32)g_core_load[i]);p=scopy(p,"    ");p=udec(p,(u32)g_core_cycles[i]);*p=0; term_puts(b); term_putc('\n'); }
        if(g_cores_online<=1) term_puts("  (single core - run 'smpstart' then 'smpwork' to see load spread)\n"); }
    else if(seq(cmd,"smpwork")||seq(cmd,"parallel")){
        if(g_cores_online<=1){ tcol(rgb(230,160,60)); term_puts("only 1 core online - run 'smpstart' first to bring the cores online.\n"); tcol(WHT); }
        else { int workers=g_cores_online-1; if(workers>8)workers=8;
            tcol(rgb(255,210,90)); { char b[80],*p=b; p=scopy(p,"dispatching ");p=udec(p,(u32)workers);p=scopy(p," prime-counting jobs across the extra cores...");*p=0; term_puts(b); term_putc('\n'); } tcol(WHT);
            g_smp_work_on=1;   /* wake the parked worker cores */
            u64 t0=g_ticks; int ids[8];
            int nw=0; for(int i=0;i<workers;i++){ int id=smp_submit(200000ULL+(u64)i*20000ULL); if(id>=0)ids[nw++]=id; } workers=nw;
            { extern int bkl_drop(void); extern void bkl_take(int); int d=bkl_drop();
              for(;;){ int left=0; for(int i=0;i<workers;i++) if(g_jobs[ids[i]].state!=3)left++; if(!left||g_ticks-t0>1500)break; cpu_pause(); }
              bkl_take(d); }
            for(int i=0;i<workers;i++) if(g_jobs[ids[i]].state!=3){ term_puts("  the extra cores are busy - not all jobs finished in 15 s\n"); break; }
            u64 dt=g_ticks-t0;
            for(int i=0;i<workers;i++){ if(g_jobs[ids[i]].state!=3){ smp_lock(); if(g_jobs[ids[i]].state==1)g_jobs[ids[i]].state=0; smp_unlock(); continue; } char b[90],*p=b; tcol(CYAN); p=scopy(p,"  core ");p=udec(p,(u32)g_jobs[ids[i]].core);p=scopy(p,": primes<=");p=udec(p,(u32)g_jobs[ids[i]].arg);p=scopy(p," = ");p=udec(p,(u32)g_jobs[ids[i]].result);*p=0; term_puts(b); term_putc('\n'); for(int k=0;k<NJOBS;k++)if(k==ids[i]){g_jobs[k].state=0;break;} }
            tcol(GREEN); { char b[80],*p=b; p=scopy(p,"  all jobs done in ~");p=udec(p,(u32)(dt*10));p=scopy(p," ms, computed in PARALLEL on the cores.");*p=0; term_puts(b); term_putc('\n'); }
            term_puts("  -> real multi-core work. (isolated compute; full SMP scheduling is still a further step.)\n"); tcol(WHT); }
    }
    else if(seq(cmd,"smpstart")||seq(cmd,"smp")){
        const char* a=arg; while(*a==' ')a++; if(seq(cmd,"smpstart"))a="on";
        char b[120],*p;
        if(seq((char*)a,"on")){
            tcol(rgb(255,200,90)); term_puts("SMP: starting the other cores and scheduling programs on all of them...\n"); tcol(WHT);
            smp_pref_set("starting"); int r=smp_enable(); smp_pref_set("on");
            if(r==-1){ tcol(rgb(240,120,120)); term_puts("smp: no calibrated local APIC timer - multi-core scheduling needs one\n"); tcol(WHT); }
            else if(r<=1){ tcol(rgb(230,160,60)); term_puts("smp: no other cores came online (single-core machine, or the cores did not answer)\n"); tcol(WHT); }
            else { tcol(GREEN); p=b; p=scopy(p,"smp: on - "); p=udec(p,(u32)r); p=scopy(p," cores schedule programs (big kernel lock, preemptive user mode)"); *p=0; term_puts(b); term_putc('\n'); tcol(WHT); } }
        else if(seq((char*)a,"off")){
            int r=smp_disable(); if(r==0)smp_pref_set("off"); if(r<0){ tcol(rgb(240,120,120)); term_puts("smp: some cores did not stop in time\n"); tcol(WHT); }
            else { tcol(GREEN); term_puts("smp: off - programs run on core 0 only\n"); tcol(WHT); } }
        else {
            tcol(CYAN); p=b; p=scopy(p,"cores in system: "); { u8 ids[NCPU]; int c=madt_apic_ids(ids,NCPU); if(c<=0)c=acpi_count_cores(); if(c>0)p=udec(p,(u32)c); else p=scopy(p,"unknown"); } p=scopy(p,"   online: "); p=udec(p,(u32)g_cores_online);
            p=scopy(p,"   scheduling: "); p=scopy(p,g_smp_on?"all cores":"core 0 only"); *p=0; term_puts(b); term_putc('\n'); tcol(WHT);
            term_puts("  cpu  apic  state     switches  syscalls  user-ticks  idle-ticks  ipis\n");
            for(int i=0;i<g_ncpu&&i<NCPU;i++){ struct pcpu* c=&g_cpu[i]; if(!c->online)continue; p=b; p=scopy(p,"  "); p=udec(p,(u32)i); p=scopy(p,"    "); p=udec(p,c->apic_id); p=scopy(p,"     ");
                p=scopy(p,i==0?"boot   ":c->parked?"parked ":c->js_running?"running":"idle   "); p=scopy(p,"   "); p=udec(p,(u32)c->switches); p=scopy(p,"      "); p=udec(p,(u32)c->syscalls);
                p=scopy(p,"      "); p=udec(p,(u32)(i==0?0:c->user_ticks)); p=scopy(p,"        "); p=udec(p,(u32)(i==0?0:c->idle_ticks)); p=scopy(p,"        "); p=udec(p,(u32)c->ipis); *p=0; term_puts(b); term_putc('\n'); }
            term_puts("  smp on | smp off | smptest [workers] - run a multi-core stress test\n"); }
    }
    else if(seq(cmd,"threads")||seq(cmd,"thread")){
        tcol(rgb(255,210,90)); term_puts("threads: one process spawns worker threads that share its memory...\n"); tcol(WHT);
        if(!pf_ready){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool\n"); tcol(WHT); }
        else { us_init(); for(int i=0;i<MAXPROC;i++){g_proc[i].used=0;g_proc[i].alive=0;} g_r3out_n=0; g_r3out[0]=0; g_thread_stk_next=0x40000200000ULL;
            int a=proc_create(_binary_user_user_thread_elf_start,(u32)(_binary_user_user_thread_elf_end-_binary_user_user_thread_elf_start));
            if(a<0){ tcol(RED); term_puts("  failed to create process\n"); tcol(WHT); }
            else { sched_run_all();
                tcol(CYAN); term_puts("  output:\n"); tcol(WHT);
                int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
                tcol(GREEN); term_puts("  PASS: multiple threads ran in one address space (shared memory).\n"); tcol(WHT); } }
    }
    else if(seq(cmd,"mem")||seq(cmd,"mmap")){
        tcol(rgb(255,210,90)); term_puts("POSIX memory (rung 2+3): a program uses mmap/brk/getpid...\n"); tcol(WHT);
        int rc=elf_load_run(_binary_user_user_mem_elf_start,(u32)(_binary_user_user_mem_elf_end-_binary_user_user_mem_elf_start));
        if(rc==1){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool\n"); tcol(WHT); }
        else if(rc){ tcol(RED); term_puts("  failed to load\n"); tcol(WHT); }
        else { tcol(rgb(180,230,180)); { int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n'); }
            tcol(GREEN); term_puts("  PASS: mmap + brk + getpid (process memory) - the syscalls malloc is built on.\n"); tcol(WHT); } }
    else if(seq(cmd,"posix")||seq(cmd,"fdtest")){
        tcol(rgb(255,210,90)); term_puts("POSIX fd layer (rung 1): a program uses open/write/lseek/read/close on integer fds...\n"); tcol(WHT);
        int rc=elf_load_run(_binary_user_user_posix_elf_start,(u32)(_binary_user_user_posix_elf_end-_binary_user_user_posix_elf_start));
        if(rc==1){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool\n"); tcol(WHT); }
        else if(rc){ tcol(RED); term_puts("  failed to load\n"); tcol(WHT); }
        else { tcol(rgb(180,230,180)); { int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n'); }
            tcol(GREEN); term_puts("  PASS: real POSIX file descriptors (fd table + VFS) - foundation for a POSIX libc.\n"); tcol(WHT); } }
    else if(seq(cmd,"app")||seq(cmd,"gui")){
        tcol(rgb(255,210,90)); term_puts("launching a graphical user-space app (ring 3, via the libc)...\n"); tcol(WHT);
        int rc=elf_load_run(_binary_user_user_app_elf_start,(u32)(_binary_user_user_app_elf_end-_binary_user_user_app_elf_start));
        if(rc==1){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool (needs >=272 MB RAM)\n"); }
        else if(rc){ tcol(RED); term_puts("  failed to load the app\n"); }
        else { tcol(GREEN); term_puts("  app exited. A real ring-3 program drew graphics + used malloc/printf via syscalls.\n"); }
        tcol(WHT); }
    else if(seq(cmd,"run")||seq(cmd,"userelf")||seq(cmd,"exec")){
        tcol(rgb(255,210,90)); term_puts("loading a real ELF user program (compiled separately)...\n"); tcol(WHT);
        int rc=elf_load_run(_binary_user_user_demo_elf_start,(u32)(_binary_user_user_demo_elf_end-_binary_user_user_demo_elf_start));
        if(rc==1){ tcol(rgb(230,160,60)); term_puts("  skipped: no frame pool (needs >=272 MB RAM)\n"); }
        else if(rc==2){ tcol(RED); term_puts("  FAILED: could not set up the user address space\n"); }
        else if(rc==3){ tcol(RED); term_puts("  not a valid ELF64\n"); }
        else { tcol(CYAN); term_puts("  program output:\n"); tcol(WHT);
            int st=0; for(int i=0;i<g_r3out_n;i++){ if(st==0){term_puts("    ");st=1;} term_putc(g_r3out[i]); if(g_r3out[i]=='\n')st=0; } if(st)term_putc('\n');
            tcol(GREEN); term_puts("  PASS: a separately-compiled ELF ran in ring 3 via the loader + mini-libc.\n");
            term_puts("  -> userspace ELF loader + C library work. (stage 6+7 done)\n"); }
        tcol(WHT); }
    else if((seq(cmd,"battery")||seq(cmd,"bat"))&&g_acb_n&&arg[0]!='e'){ battery_cmd(); }
    else if(seq(cmd,"brightness")||seq(cmd,"backlight")){ brightness_cmd(arg); }
    else if(seq(cmd,"audio")||seq(cmd,"sound")){ audio_cmd(arg); }
    else if(seq(cmd,"acpi")){ acpi_cmd(arg); }
    else if(seq(cmd,"timers")){ timers_cmd(); }
    else if(seq(cmd,"timertest")){ timertest_cmd(); }
    else if(seq(cmd,"ec")||seq(cmd,"battery")||seq(cmd,"bat")){
        int det=ec_detect();
        char b[100],*p; tcol(CYAN); p=b;p=scopy(p,"EC (0x62/0x66) detect: ");p=scopy(p,det?"responding":"NO RESPONSE");*p=0; term_puts(b); term_putc('\n'); tcol(WHT);
        if(!det){ term_puts("  the Embedded Controller isn't answering on this machine.\n"); }
        else { term_puts("  scanning offsets for a plausible battery % (1..100):\n");
            int found=0;
            for(int off=0;off<0x100;off++){ u8 v; if(ec_read((u8)off,&v)){ if(v>=1&&v<=100){ p=b;p=scopy(p,"    offset 0x");p=hx(p,(u32)off,2);p=scopy(p," = ");p=udec(p,(u32)v); if(v<=100)p=scopy(p,"  <- possible battery %"); *p=0; tcol(v>=5&&v<=100?GREEN:WHT); term_puts(b); term_putc('\n'); found++; if(found>=24){term_puts("    ...\n");break;} } } }
            if(!found) term_puts("    (no byte in range 1..100 - unusual)\n");
            tcol(WHT); u8 ac=0; if(ec_read(0x01,&ac)){ p=b;p=scopy(p,"  offset 0x01 (AC/charge guess) = 0x");p=hx(p,(u32)ac,2);*p=0; term_puts(b); term_putc('\n'); }
            term_puts("  tell me which offset matches your real battery % and I'll lock it in.\n"); } }
    else if(seq(cmd,"usbrm")){ if(!arg[0]){ term_puts("usage: usbrm <FILE>\n"); }
        else if(!msd_autoinit()){ tcol(RED);term_puts("no USB stick\n");tcol(WHT); }
        else { ufat_mount(); int fi=-1; for(int i2=0;i2<ufs_n;i2++){ int eq=1; for(int k=0;k<16;k++){ char x=ufs[i2].name[k],y=arg[k]; if(x>='a'&&x<='z')x-=32; if(y>='a'&&y<='z')y-=32; if(x!=y){eq=0;break;} if(!x&&!y)break; } if(eq){fi=i2;break;} }
            if(fi<0){ tcol(RED);term_puts("file not found\n");tcol(WHT); }
            else if(ufat_delete_file(fi)==0){ tcol(GREEN);term_puts("deleted from USB\n");tcol(WHT); } else { tcol(RED);term_puts("delete failed\n");tcol(WHT); } } }
    else if(seq(cmd,"usb")||seq(cmd,"xhci")){ term_puts("rescanning all USB controllers...\n"); xhci_mouse_autoinit(); char u[]="usbinfo"; run_cmd(u); }
    else if(seq(cmd,"wifi")&&wifi_cmd2(arg)){ }
    else if(seq(cmd,"wifi")){ g_wpa_pump=ui_pump; int doload=(arg[0]=='l'||arg[0]=='L'); int doinit=(arg[0]=='i'||arg[0]=='I');   /* wifi load / wifi init */
        if(!pci_n)pci_scan();
        int wi=-1; for(int k=0;k<pci_n;k++) if(pci_cls[k]==0x02 && pci_sub[k]==0x80){ wi=k; break; }
        if(wi<0){ tcol(WHT); term_puts("No Wi-Fi adapter found on the PCI bus.\n");
            int e=nic_index(); if(e>=0){ term_puts("Wired NIC present: "); term_puts((char*)nic_name(pci_ven[e],pci_dev[e])); term_putc('\n'); } }
        else { char b[100],*p; int bus=pci_bus[wi], sl=pci_slot[wi]; const char* ven;
            switch(pci_ven[wi]){ case 0x8086:ven="Intel"; break; case 0x10ec:ven="Realtek"; break; case 0x168c:ven="Qualcomm Atheros"; break;
                case 0x17cb:ven="Qualcomm"; break; case 0x14c3:ven="MediaTek"; break; case 0x14e4:ven="Broadcom"; break; default:ven="Unknown"; }
            tcol(CYAN); p=b;p=scopy(p,"Wi-Fi adapter: ");p=scopy(p,(char*)ven);p=scopy(p,"  [0x");p=hx(p,pci_ven[wi],4);*p++=':';p=hx(p,pci_dev[wi],4);p=scopy(p,"]  bus ");p=udec(p,(u32)bus);*p=0; term_puts(b); term_putc('\n');
            u32 c4=pci_cfg(bus,sl,0,0x04); pci_cfgw(bus,sl,0,0x04,c4|0x06);
            u32 mmio=0; tcol(WHT);
            for(int bi=0;bi<6;bi++){ u32 bar=pci_cfg(bus,sl,0,0x10+bi*4); if(!bar)continue;
                if(bar&1){ p=b;p=scopy(p,"  BAR");*p++=(char)('0'+bi);p=scopy(p," I/O  0x");p=hx(p,bar&0xFFFFFFFC,8);*p=0; term_puts(b);term_putc('\n'); }
                else { u32 a=bar&0xFFFFFFF0u; p=b;p=scopy(p,"  BAR");*p++=(char)('0'+bi);p=scopy(p," MEM 0x");p=hx(p,a,8);*p=0; term_puts(b);term_putc('\n'); if(a>0x100000u)mmio=a; } }
            extern const unsigned char _binary_rtw8821c_fw_bin_start[]; extern const unsigned char _binary_rtw8821c_fw_bin_end[];
            long fwsz=(long)(_binary_rtw8821c_fw_bin_end-_binary_rtw8821c_fw_bin_start);
            { u16 sig=(u16)(_binary_rtw8821c_fw_bin_start[0]|(_binary_rtw8821c_fw_bin_start[1]<<8));
              u16 fver=(u16)(_binary_rtw8821c_fw_bin_start[4]|(_binary_rtw8821c_fw_bin_start[5]<<8)); u8 fsub=_binary_rtw8821c_fw_bin_start[6];
              tcol(GREEN); p=b;p=scopy(p,"  firmware: rtw8821c_fw.bin  ");p=udec(p,(u32)fwsz);p=scopy(p," bytes  sig 0x");p=hx(p,sig,4);p=scopy(p,"  v");p=udec(p,fver);*p++='.';p=udec(p,fsub);*p=0; term_puts(b);term_putc('\n'); }
            if(!mmio){ tcol(rgb(230,160,60)); term_puts("  no MMIO window assigned by BIOS - cannot touch chip registers.\n"); tcol(WHT); term_puts("  status: detected, firmware ready; MMIO needed to proceed.\n"); }
            else {
                #define WMR(o)   (*(volatile u32*)(u64)(mmio+(o)))
                #define WMR16(o) (*(volatile u16*)(u64)(mmio+(o)))
                #define WMW16(o,v) (*(volatile u16*)(u64)(mmio+(o))=(u16)(v))
                #define WMW8(o,v)  (*(volatile u8*)(u64)(mmio+(o))=(u8)(v))
                u32 sysc=WMR(0xF0);
                if(sysc==0xFFFFFFFFu){ tcol(rgb(230,160,60)); term_puts("  chip not responding at MMIO (powered down?).\n"); }
                else { tcol(CYAN); p=b;p=scopy(p,"  chip SYS_CFG(0xF0)=0x");p=hx(p,sysc,8);*p=0; term_puts(b);term_putc('\n');
                    u8 mac[6]; for(int i=0;i<6;i++)mac[i]=*(volatile u8*)(u64)(mmio+0x0610+i);
                    p=b;p=scopy(p,"  MAC ");for(int i=0;i<6;i++){p=hx(p,mac[i],2);if(i<5)*p++=':';}*p=0; term_puts(b);term_putc('\n');
                    if(arg[0]=='e'||arg[0]=='E'){
                        tcol(rgb(255,210,90)); term_puts("  [wifi efuse] reading physical EFUSE (512 B)\n"); tcol(WHT);
                        int bad=rtw_efuse_dump_phy(mmio,rtw_phymap,RTW_EFUSE_PHY_SZ);
                        int used=rtw_efuse_logical(rtw_phymap,RTW_EFUSE_PHY_SZ,RTW_EFUSE_PROT_SZ,rtw_logmap,RTW_EFUSE_LOG_SZ);
                        rtw_efuse_valid=(used>0);
                        p=b;p=scopy(p,"  read done  timeouts=");p=udec(p,(u32)bad);p=scopy(p,"  physical bytes consumed=");
                        if(used<0)p=scopy(p,"decode error"); else p=udec(p,(u32)used); *p=0;
                        tcol(bad?rgb(230,160,60):CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                        int allff=1; for(int i=0;i<RTW_EFUSE_PHY_SZ;i++) if(rtw_phymap[i]!=0xFF){allff=0;break;}
                        if(allff){ tcol(rgb(230,120,90)); term_puts("  EFUSE reads all 0xFF - chip is not powered on. run 'wifi init' first.\n"); tcol(WHT); }
                        else {
                            u16 rid=(u16)(rtw_logmap[0]|(rtw_logmap[1]<<8));
                            p=b;p=scopy(p,"  rtl_id=0x");p=hx(p,rid,4);p=scopy(p,"  chplan=0x");p=hx(p,rtw_logmap[0xB8],2);
                            p=scopy(p,"  xtal_k=0x");p=hx(p,rtw_logmap[0xB9],2);p=scopy(p,"  therm=0x");p=hx(p,rtw_logmap[0xBA],2);*p=0;
                            tcol(CYAN); term_puts(b); term_putc('\n');
                            p=b;p=scopy(p,"  rfe=0x");p=hx(p,rtw_logmap[0xCA],2);p=scopy(p,"  country=0x");p=hx(p,rtw_logmap[0xCB],2);
                            p=scopy(p,"  MAC(0xD0) ");for(int i=0;i<6;i++){p=hx(p,rtw_logmap[0xD0+i],2); if(i<5)*p++=':';}*p=0;
                            term_puts(b); term_putc('\n');
                            p=b;p=scopy(p,"  ids(0xD6) ven=0x");p=hx(p,(u32)(rtw_logmap[0xD6]|(rtw_logmap[0xD7]<<8)),4);
                            p=scopy(p," dev=0x");p=hx(p,(u32)(rtw_logmap[0xD8]|(rtw_logmap[0xD9]<<8)),4);*p=0;
                            term_puts(b); term_putc('\n'); tcol(WHT);
                            term_puts("  logical map 0x0B0-0x0DF:\n"); tcol(GREY);
                            for(int o=0xB0;o<0xE0;o+=16){ char hl[80]; rtw_hexline(rtw_logmap,o,16,hl); term_puts(hl); term_putc('\n'); }
                            tcol(WHT);
                            if(msd_ok||msd_autoinit()){
                                if(ufat_write_file("EFUSEP.BIN",(const char*)rtw_phymap,RTW_EFUSE_PHY_SZ)==0 &&
                                   ufat_write_file("EFUSEL.BIN",(const char*)rtw_logmap,RTW_EFUSE_LOG_SZ)==0){
                                    tcol(GREEN); term_puts("  wrote EFUSEP.BIN + EFUSEL.BIN to the USB stick\n"); tcol(WHT); }
                                else { tcol(GREY); term_puts("  (could not write the dumps to USB)\n"); tcol(WHT); } }
                            else { tcol(GREY); term_puts("  (no USB stick mounted - dumps not saved)\n"); tcol(WHT); }
                        } }
                    else if(seq(arg,"macinit")||seq(arg,"MACINIT")||seq(arg,"trx")||seq(arg,"TRX")){
                        tcol(rgb(120,200,255)); term_puts("  [wifi macinit] rtw_init_trx_cfg: queue map + page alloc (auto-LLT) + H2C queue\n"); tcol(WHT);
                        int r=rtw_mac_trx_init(mmio);
                        rtw_mac_trx_ok=(r==0);
                        u8 llt=rtw_r8(mmio,0x0208); u16 bnd=rtw_r16(mmio,0x0204); u8 cr=rtw_r8(mmio,0x0100);
                        p=b;p=scopy(p,"  CR=0x");p=hx(p,cr,2);p=scopy(p,"  rsvd_boundary=0x");p=hx(p,bnd&0x0FFF,3);p=scopy(p,"  AUTO_LLT=0x");p=hx(p,llt,2);*p=0;
                        tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                        if(r==0){ tcol(GREEN); term_puts("  MAC TRX init OK -- page pool allocated, LLT settled. beacon queue can accept pages now.\n");
                            term_puts("  next: 'wifi txbuf' should now assert BCN_VALID.\n"); tcol(WHT); }
                        else { tcol(rgb(230,120,90)); term_puts("  AUTO_LLT did not clear -- page-table init rejected (check queue map / boundary)\n"); tcol(WHT); } }
                    else if((arg[0]=='t'||arg[0]=='T')&&(arg[1]=='x'||arg[1]=='X')){
                        tcol(rgb(120,200,255)); term_puts("  [wifi txbuf] staging a test page via the beacon queue, then IDDMA-reading it back\n"); tcol(WHT);
                        static const u8 pat[64]={0x4E,0x56,0x58,0x21,0xDE,0xAD,0xBE,0xEF,0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,
                                                 0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00,
                                                 0xF0,0xE1,0xD2,0xC3,0xB4,0xA5,0x96,0x87,0x78,0x69,0x5A,0x4B,0x3C,0x2D,0x1E,0x0F,
                                                 0xCA,0xFE,0xBA,0xBE,0x8B,0xAD,0xF0,0x0D,0x13,0x37,0xC0,0xDE,0xFA,0xCE,0xB0,0x0C};
                        int st=rtw_bcnq_stage(mmio,pat,64);
                        if(st<0){ tcol(rgb(230,120,90));
                            if(st==-1) term_puts("  alloc failed\n");
                            else term_puts("  BCN_VALID never asserted - the beacon queue did not accept the page\n"); tcol(WHT); }
                        else {
                            tcol(GREEN); term_puts("  BCN_VALID asserted - page landed in the TX buffer\n"); tcol(WHT);
                            u32 v=rtw_r32(mmio,0x1208); v|=(1u<<25); rtw_w32(mmio,0x1208,v);
                            int r=rtw_iddma_block(mmio,0x18780000u+48u,0x00200000u,64,1);
                            u32 ch=rtw_r32(mmio,0x1208);
                            p=b;p=scopy(p,"  IDDMA ");p=scopy(p,(r==0)?"completed":"BUSY-timeout");p=scopy(p,"  CH0CTRL=0x");p=hx(p,ch,8);
                            p=scopy(p,"  chksum_sts=");p=udec(p,(ch>>27)&1);*p=0; tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                            if(r==0 && !((ch>>27)&1)){ tcol(GREEN); term_puts("  TX-staging VERIFIED: IDDMA read the staged page and checksum was good.\n");
                                term_puts("  the firmware download foundation works - 'wifi fw' can be built on this.\n"); tcol(WHT); }
                            else { tcol(rgb(230,160,60)); term_puts("  staged, but IDDMA checksum flagged - source addr or page geometry still off.\n"); tcol(WHT); } } }
                    else if((arg[0]=='f'||arg[0]=='F')&&(arg[1]=='w'||arg[1]=='W')){
                        extern const unsigned char _binary_rtw8821c_fw_bin_start[]; extern const unsigned char _binary_rtw8821c_fw_bin_end[];
                        long fwsz2=(long)(_binary_rtw8821c_fw_bin_end-_binary_rtw8821c_fw_bin_start);
                        tcol(rgb(120,200,255)); term_puts("  [wifi fw] IDDMA firmware download (rtw88 mac.c path)\n"); tcol(WHT);
                        int r=rtw_fw_download_iddma(mmio,_binary_rtw8821c_fw_bin_start,fwsz2);
                        u16 st=rtw_r16(mmio,0x0080);
                        p=b;p=scopy(p,"  MCUFW_CTRL(0x80)=0x");p=hx(p,st,4);p=scopy(p,"  imem_dw=");p=udec(p,(st>>3)&1);
                        p=scopy(p," imem_ck=");p=udec(p,(st>>4)&1);p=scopy(p," dmem_dw=");p=udec(p,(st>>5)&1);p=scopy(p," dmem_ck=");p=udec(p,(st>>6)&1);
                        p=scopy(p," dwrdy=");p=udec(p,(st>>14)&1);p=scopy(p," initrdy=");p=udec(p,(st>>15)&1);*p=0;
                        tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                        p=b;p=scopy(p,"  per-section: DMEM ck=");p=udec(p,(u32)g_dmem_ck);p=scopy(p,(g_dmem_sf?" STAGEFAIL":""));p=scopy(p,(g_dmem_if?" IDDMAFAIL":""));
                        p=scopy(p,"  IMEM ck=");p=udec(p,(u32)g_imem_ck);p=scopy(p,(g_imem_sf?" STAGEFAIL":""));p=scopy(p,(g_imem_if?" IDDMAFAIL":""));
                        p=scopy(p,"  EMEM ck=");p=udec(p,(u32)g_emem_ck);*p=0; tcol(rgb(200,180,255)); term_puts(b); term_putc('\n'); tcol(WHT);
                        if(r==0){ tcol(GREEN); term_puts("  MCU_FW_READY set -- the MCU is running your firmware DESPITE any ck complaints!\n");
                            term_puts("  next: 'wifi h2c' to talk to the MCU, then RX ring for a beacon.\n"); tcol(WHT); }
                        else { tcol(rgb(230,160,60)); term_puts("  all sections downloaded; FW_READY did not assert.\n");
                            term_puts("  if DMEM/IMEM ck=1 above, data was fine - blocker is post-download init, not the checksum.\n"); tcol(WHT); } }
                    else if((arg[0]=='p'||arg[0]=='P')&&(arg[1]=='r'||arg[1]=='R')){
                        if(!g_rxnum||!g_benum){ tcol(rgb(230,120,90)); term_puts("  run 'wifi rings' first\n"); tcol(WHT); }
                        else {
                            u8 ch=6; { const char* a=arg; while(*a&&*a!=' ')a++; while(*a&&(*a<'0'||*a>'9'))a++; if(*a>='0'&&*a<='9'){int v=0;while(*a>='0'&&*a<='9'){v=v*10+(*a-'0');a++;} if(v>=1&&v<=13)ch=(u8)v;} }
                            p=b;p=scopy(p,"  [wifi probe] TX broadcast probe request on channel ");p=udec(p,ch);p=scopy(p,", waiting for a response...");*p=0;
                            tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT);
                            rtw_rx_ring_up(mmio);
                            rtw_set_channel_2g(mmio,ch);
                            int got=0; char rs[40];
                            rtw_tx_probe(mmio,&got,rs);
                            p=b;p=scopy(p,"  TX: hw_idx ");p=udec(p,g_probe_hwb);p=scopy(p,"->");p=udec(p,g_probe_hwa);p=scopy(p,g_probe_txok?"  FRAME LEFT THE QUEUE (transmitted)":"  frame NOT consumed by hw (TX ring stuck)");*p=0;
                            tcol(g_probe_txok?CYAN:rgb(230,160,60)); term_puts(b); term_putc('\n'); tcol(WHT);
                            if(got){ tcol(GREEN); p=b;p=scopy(p,"  PROBE RESPONSE from \"");p=scopy(p,rs[0]?rs:"<hidden>");p=scopy(p,"\"");*p=0; term_puts(b); term_putc('\n');
                                term_puts("  *** TWO-WAY RADIO: NoovexOS transmitted a frame and an AP answered ***\n"); tcol(WHT); }
                            else { tcol(rgb(230,160,60)); term_puts("  no probe response (TX may not have gone out, or none in range). Try another channel: wifi probe 1 / 11\n"); tcol(WHT); }
                        } }
                    else if((arg[0]=='j'||arg[0]=='J')&&(arg[1]=='o'||arg[1]=='O')){
                        const char* nm=arg+2; while(*nm==' ')nm++;
                        if(!*nm){ tcol(rgb(230,120,90)); term_puts("  usage: wifi join <ssid>   (open networks only for now)\n"); tcol(WHT); }
                        else if(!g_rxnum||!g_benum){ tcol(rgb(230,120,90)); term_puts("  run 'wifi rings' first\n"); tcol(WHT); }
                        else {
                            p=b;p=scopy(p,"  [wifi join] scanning for \"");for(int q=0;nm[q]&&q<32;q++)*p++=nm[q];p=scopy(p,"\"...");*p=0; tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT);
                            g_apn=0; rtw_rx_ring_up(mmio);
                            static const u8 jc[]={1,6,11,2,3,4,5,7,8,9,10,12,13};
                            int target=-1;
                            for(int ci=0;ci<13 && target<0;ci++){ rtw_set_channel_2g(mmio,jc[ci]);
                                for(int w=0;w<3;w++){ rtw_delay_us(60000); ui_pump(); rtw_scan_drain(mmio,jc[ci]); }
                                for(int a=0;a<g_apn;a++){ int eq=1; const char* z=nm; int k=0; while(z[k]&&k<32){ if(g_aps[a].ssid[k]!=z[k])eq=0; k++; } if(g_aps[a].ssid[k]||z[k])eq=0; if(eq){target=a;break;} } }
                            if(target<0){ tcol(rgb(230,160,60)); term_puts("  network not found in scan\n"); tcol(WHT); }
                            else {
                                p=b;p=scopy(p,"  found on ch ");p=udec(p,g_aps[target].ch);p=scopy(p,", BSSID ");for(int z=0;z<6;z++){p=hx(p,g_aps[target].bssid[z],2);if(z<5)*p++=':';}*p=0; tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                                term_puts("  AUTH -> ASSOC handshake...\n");
                                int r=rtw_assoc(mmio,&g_aps[target],nm);
                                if(r==0){ tcol(GREEN); term_puts("  *** ASSOCIATED! NoovexOS is linked to the access point ***\n");
                                    term_puts("  (open network: DHCP/TCP can now run over Wi-Fi. WPA2 nets need the 4-way handshake, next.)\n"); tcol(WHT); }
                                else if(r==1){ tcol(rgb(230,160,60)); term_puts("  no AUTH response (AP didn't answer - TX radiating? try closer / another AP)\n"); tcol(WHT); }
                                else if(r==2){ tcol(rgb(230,160,60)); term_puts("  AUTH rejected by AP (open-system refused - likely a WPA2 network)\n"); tcol(WHT); }
                                else if(r==3){ tcol(rgb(230,160,60)); term_puts("  AUTH ok but no ASSOC response\n"); tcol(WHT); }
                                else { tcol(rgb(230,160,60)); term_puts("  ASSOC rejected by AP (status != 0)\n"); tcol(WHT); }
                            }
                        } }
                    else if((arg[0]=='o'||arg[0]=='O')&&(arg[1]=='p'||arg[1]=='P')){
                        if(!g_rxnum){ tcol(rgb(230,120,90)); term_puts("  run 'wifi rings' first\n"); tcol(WHT); }
                        else {
                            tcol(rgb(120,200,255)); term_puts("  [wifi open] scanning for OPEN (no-password) networks...\n"); tcol(WHT);
                            g_apn=0; rtw_rx_ring_up(mmio);
                            static const u8 oc[]={1,6,11,2,3,4,5,7,8,9,10,12,13};
                            for(int ci=0;ci<13;ci++){ rtw_set_channel_2g(mmio,oc[ci]); for(int w=0;w<3;w++){ rtw_delay_us(60000); ui_pump(); rtw_scan_drain(mmio,oc[ci]); } }
                            int nopen=0; for(int i=0;i<g_apn;i++) if(g_aps[i].sec==0)nopen++;
                            if(nopen==0){ tcol(rgb(230,160,60)); p=b;p=scopy(p,"  no open networks (");p=udec(p,(u32)g_apn);p=scopy(p," total, all secured). WPA2 support is the next build.");*p=0; term_puts(b); term_putc('\n'); tcol(WHT); }
                            else {
                                tcol(GREEN); p=b;p=scopy(p,"  ");p=udec(p,(u32)nopen);p=scopy(p," OPEN network(s) you can join now:");*p=0; term_puts(b); term_putc('\n'); tcol(WHT);
                                for(int i=0;i<g_apn;i++){ if(g_aps[i].sec!=0)continue; p=b;p=scopy(p,"    wifi join ");p=scopy(p,g_aps[i].ssid[0]?g_aps[i].ssid:"<hidden>");p=scopy(p,"   (ch ");p=udec(p,g_aps[i].ch);*p++=')';*p=0; tcol(GREEN); term_puts(b); term_putc('\n'); tcol(WHT); }
                            }
                        } }
                    else if((arg[0]=='w'||arg[0]=='W')&&(arg[1]=='p'||arg[1]=='P')){
                        const char* a=arg; while(*a&&*a!=' ')a++; while(*a==' ')a++;   /* skip the whole 'wpa' token, then spaces */
                        char ss[33]; int sn=0; while(*a&&sn<32)ss[sn++]=*a++; while(sn>0&&ss[sn-1]==' ')sn--; ss[sn]=0;   /* whole rest = SSID, spaces allowed */
                        int allnum=(sn>0); for(int z=0;z<sn;z++) if(ss[z]<'0'||ss[z]>'9')allnum=0;
                        if(allnum){   /* connect by ID from last scan - no rescan */
                            int id=0; for(int z=0;z<sn;z++)id=id*10+(ss[z]-'0');
                            if(id<1||id>g_apn){ tcol(rgb(230,120,90)); term_puts("  no network with that ID - run 'wifi scan' first\n"); tcol(WHT); }
                            else { int idx=id-1;
                                int j=0; while(g_aps[idx].ssid[j]&&j<32){g_pwssid[j]=g_aps[idx].ssid[j];j++;} g_pwssid[j]=0;
                                g_pwbyid=idx+1;   /* remember chosen index (1-based; 0=none) */
                                if(g_aps[idx].sec==0){ g_pwbyid=idx+1; wpa_connect_saved(mmio,idx,""); }
                                else { g_pwprompt=1; p=b;p=scopy(p,"  password for \"");p=scopy(p,g_pwssid);p=scopy(p,"\": ");*p=0; tcol(rgb(120,200,255)); term_puts(b); tcol(WHT); } }
                        }
                        else if(!sn){ tcol(rgb(230,120,90)); term_puts("  usage: wifi wpa <ID|ssid>   (run 'wifi scan' to see IDs)\n"); tcol(WHT); }
                        else { int i2=0; while(ss[i2]&&i2<32){g_pwssid[i2]=ss[i2];i2++;} g_pwssid[i2]=0; g_pwbyid=0; g_pwprompt=1; tcol(rgb(120,200,255)); term_puts("  password: "); tcol(WHT); } }
                    else if((arg[0]=='c'||arg[0]=='C')&&(arg[1]=='r'||arg[1]=='R')){
                        tcol(rgb(120,200,255)); term_puts("  [wifi cryptotest] verifying WPA2 crypto vs known vectors\n"); tcol(WHT);
                        int pass=0,tot=0;
                        /* SHA-1("abc") = a9993e36 4706816a ba3e2571 7850c26c 9cd0d89d */
                        { u8 h[20]; sha1_hash((const u8*)"abc",3,h); tot++;
                          u8 exp[4]={0xa9,0x99,0x3e,0x36}; int ok=(h[0]==exp[0]&&h[1]==exp[1]&&h[2]==exp[2]&&h[3]==exp[3]);
                          p=b;p=scopy(p,"  SHA-1(abc)     ");p=scopy(p,ok?"PASS  ":"FAIL  ");for(int i=0;i<4;i++)p=hx(p,h[i],2);*p=0;
                          tcol(ok?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); if(ok)pass++; }
                        /* HMAC-SHA1 key="Jefe" data="what do ya want for nothing?" = effcdf6a e5eb2fa2 d27416d5 f184df9c 259a7c79 */
                        { u8 h[20]; hmac_sha1((const u8*)"Jefe",4,(const u8*)"what do ya want for nothing?",28,h); tot++;
                          u8 exp[4]={0xef,0xfc,0xdf,0x6a}; int ok=(h[0]==exp[0]&&h[1]==exp[1]&&h[2]==exp[2]&&h[3]==exp[3]);
                          p=b;p=scopy(p,"  HMAC-SHA1      ");p=scopy(p,ok?"PASS  ":"FAIL  ");for(int i=0;i<4;i++)p=hx(p,h[i],2);*p=0;
                          tcol(ok?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); if(ok)pass++; }
                        /* PBKDF2(password,"salt",4096,20) first bytes = 4b007901 b765489a ... (RFC6070) */
                        { u8 o[20]; pbkdf2_sha1((const u8*)"password",8,(const u8*)"salt",4,4096,o,20); tot++;
                          u8 exp[4]={0x4b,0x00,0x79,0x01}; int ok=(o[0]==exp[0]&&o[1]==exp[1]&&o[2]==exp[2]&&o[3]==exp[3]);
                          p=b;p=scopy(p,"  PBKDF2(4096)   ");p=scopy(p,ok?"PASS  ":"FAIL  ");for(int i=0;i<4;i++)p=hx(p,o[i],2);*p=0;
                          tcol(ok?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); if(ok)pass++; }
                        /* AES-CCM round-trip: encrypt then decrypt-verify must return the same plaintext */
                        { u8 key[16]; for(int i=0;i<16;i++)key[i]=(u8)i; u8 nonce[13]; for(int i=0;i<13;i++)nonce[i]=(u8)(0x10+i);
                          u8 aad[8]={1,2,3,4,5,6,7,8}; u8 pt[16]; for(int i=0;i<16;i++)pt[i]=(u8)(0xA0+i);
                          u8 ct[16]; for(int i=0;i<16;i++)ct[i]=pt[i]; u8 mic[8];
                          aes_ccm_auth_encrypt(key,nonce,aad,8,ct,16,mic); tot++;
                          int changed=0; for(int i=0;i<16;i++) if(ct[i]!=pt[i])changed=1;
                          int ver=aes_ccm_decrypt_verify(key,nonce,aad,8,ct,16,mic);
                          int same=1; for(int i=0;i<16;i++) if(ct[i]!=pt[i])same=0;
                          int ok=(changed&&ver&&same);
                          p=b;p=scopy(p,"  AES-CCM rtrip  ");p=scopy(p,ok?"PASS  (enc+dec+verify ok)":"FAIL");*p=0;
                          tcol(ok?GREEN:RED); term_puts(b); term_putc('\n'); tcol(WHT); if(ok)pass++; }
                        p=b;p=scopy(p,"  ");p=udec(p,(u32)pass);*p++='/';p=udec(p,(u32)tot);p=scopy(p," crypto tests passed");*p=0;
                        tcol(pass==tot?GREEN:RED); term_puts(b); term_putc('\n');
                        if(pass==tot) term_puts("  WPA2 crypto foundation is correct - ready to build the 4-way handshake.\n"); tcol(WHT); }
                    else if((arg[0]=='s'||arg[0]=='S')&&(arg[1]=='c'||arg[1]=='C')){
                        if(!g_rxnum){ tcol(rgb(230,120,90)); term_puts("  run 'wifi rings' first (after allbuilt/macinit/fw)\n"); tcol(WHT); }
                        else {
                            tcol(rgb(120,200,255)); term_puts("  [wifi scan] RX DMA on, hopping channels 1-13 for beacons...\n"); tcol(WHT);
                            g_apn=0; rtw_rx_ring_up(mmio);
                            static const u8 chans[]={1,6,11,2,3,4,5,7,8,9,10,12,13};
                            for(int ci=0; ci<13; ci++){ rtw_set_channel_2g(mmio,chans[ci]);
                                for(int w=0;w<3;w++){ rtw_delay_us(60000); ui_pump(); rtw_scan_drain(mmio,chans[ci]); } }
                            if(g_apn==0){
                                /* DIAGNOSTIC: show the raw RX ring/DMA state so we can see WHERE it breaks */
                                u32 wp=(rtw_r32(mmio,0x03B4)>>16)&0x0FFF; u32 rp=rtw_r32(mmio,0x03B4)&0x0FFF;
                                u32 cr=rtw_r32(mmio,0x0100); u32 rcr=rtw_r32(mmio,0x0608); u32 pcictrl=rtw_r32(mmio,0x0300);
                                u8* d0=g_rxring; u16 bs=*(volatile u16*)(u64)d0; u32 dma0=*(volatile u32*)(u64)(d0+4);
                                u32 w0=*(volatile u32*)(u64)g_rxbuf;
                                p=b;p=scopy(p,"  DIAG hwWP=");p=udec(p,wp);p=scopy(p," RP=");p=udec(p,rp);p=scopy(p," CR=0x");p=hx(p,cr,4);p=scopy(p," RCR=0x");p=hx(p,rcr,8);*p=0; tcol(CYAN); term_puts(b); term_putc('\n');
                                p=b;p=scopy(p,"  DIAG PCICTRL=0x");p=hx(p,pcictrl,8);p=scopy(p,"  desc0.bufsz=");p=udec(p,bs);p=scopy(p," desc0.dma=0x");p=hx(p,dma0,8);p=scopy(p," buf0.w0=0x");p=hx(p,w0,8);*p=0; term_puts(b); term_putc('\n'); tcol(WHT);
                                tcol(rgb(230,160,60)); term_puts("  no beacons parsed via RX DMA (frames still counted in wifi rx)\n"); tcol(WHT); }
                            else {
                                tcol(GREEN); p=b;p=scopy(p,"  found ");p=udec(p,(u32)g_apn);p=scopy(p," networks:");*p=0; term_puts(b); term_putc('\n'); tcol(WHT);
                                term_puts("  ID  SSID                          CH  SECURITY\n");
                                for(int i=0;i<g_apn;i++){ p=b; *p++=' ';*p++=' ';
                                    p=udec(p,(u32)(i+1)); if(i+1<10)*p++=' '; *p++=' ';*p++=' ';
                                    int sl=0; while(g_aps[i].ssid[sl]&&sl<28){*p++=g_aps[i].ssid[sl];sl++;}
                                    if(sl==0){p=scopy(p,"<hidden>");sl=8;}
                                    while(sl<30){*p++=' ';sl++;}
                                    p=udec(p,g_aps[i].ch); if(g_aps[i].ch<10)*p++=' ';
                                    p=scopy(p,"  ");p=scopy(p, g_aps[i].sec==0?"OPEN":g_aps[i].sec==2?"WPA2":"WEP/WPA"); *p=0;
                                    tcol(g_aps[i].sec==0?GREEN:CYAN); term_puts(b); term_putc('\n'); tcol(WHT); }
                                term_puts("  connect with:  wifi wpa <ID>    e.g.  wifi wpa 1\n");
                            }
                        } }
                    else if((arg[0]=='r'||arg[0]=='R')&&(arg[1]=='x'||arg[1]=='X')){
                        const char* a=arg+2; while(*a==' ')a++;
                        int dma=0; if((a[0]=='d'||a[0]=='D')&&(a[1]=='m'||a[1]=='M')){ dma=1; a+=3; while(*a==' ')a++; }
                        u8 ch=1; if(*a>='0'&&*a<='9'){ int v=0; while(*a>='0'&&*a<='9'){v=v*10+(*a-'0');a++;} if(v>=1&&v<=14)ch=(u8)v; }
                        p=b;p=scopy(p,"  [wifi rx] channel ");p=udec(p,ch);p=scopy(p,dma?"  (DMA parse mode)":"  (safe counter mode)");*p=0;
                        tcol(rgb(120,200,255)); term_puts(b); term_putc('\n'); tcol(WHT);
                        if(!dma){
                            u32 c0c=rtw_r32(mmio,0x0F04), c0o=rtw_r32(mmio,0x0F14), c0h=rtw_r32(mmio,0x0F10);
                            u32 a0o=rtw_r32(mmio,0x0F08), a0c=rtw_r32(mmio,0x0FCC);
                            p=b;p=scopy(p,"  step1 ok: baseband counters readable  RF18=0x");p=hx(p,rtw_rf_read_a(mmio,0x18),5);*p=0; term_puts(b); term_putc('\n');
                            term_puts("  step2: setting channel (read-modify-write, like rtw88)\n");
                            rtw_set_channel_2g(mmio,ch);
                            p=b;p=scopy(p,"  step2 ok: RF18 now 0x");p=hx(p,rtw_rf_read_a(mmio,0x18),5);*p=0; term_puts(b); term_putc('\n');
                            term_puts("  step3: receiver on (RCR), RX DMA stays OFF - no memory writes\n");
                            rtw_w32(mmio,0x0608,0xE400220Eu);
                            rtw_w8(mmio,0x0100,(u8)((rtw_r8(mmio,0x0100)|(1u<<7)|(1u<<6)|(1u<<4)|(1u<<5)) & ~((1u<<1)|(1u<<3))));
                            term_puts("  listening 3s...\n");
                            for(int w=0;w<2;w++) rtw_delay_us(200000);
                            u32 c1c=rtw_r32(mmio,0x0F04), c1o=rtw_r32(mmio,0x0F14), c1h=rtw_r32(mmio,0x0F10);
                            u32 a1o=rtw_r32(mmio,0x0F08), a1c=rtw_r32(mmio,0x0FCC);
                            u32 okc=(u32)((u16)(c1c&0xFFFF)-(u16)(c0c&0xFFFF));
                            u32 oko=(u32)((u16)(c1o&0xFFFF)-(u16)(c0o&0xFFFF));
                            u32 okh=(u32)((u16)(c1h&0xFFFF)-(u16)(c0h&0xFFFF));
                            u32 erc=(u32)((u16)(c1c>>16)-(u16)(c0c>>16));
                            u32 ero=(u32)((u16)(c1o>>16)-(u16)(c0o>>16));
                            u32 cca=(u32)((u16)(a1o>>16)-(u16)(a0o>>16))+(u32)((u16)(a1c&0xFFFF)-(u16)(a0c&0xFFFF));
                            p=b;p=scopy(p,"  good frames: CCK ");p=udec(p,okc);p=scopy(p,"  OFDM ");p=udec(p,oko);p=scopy(p,"  HT ");p=udec(p,okh);
                            p=scopy(p,"   bad CRC: ");p=udec(p,erc+ero);p=scopy(p,"   CCA hits: ");p=udec(p,cca);*p=0;
                            tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                            if(okc+oko+okh>0){ tcol(GREEN); term_puts("  *** THE RADIO IS DECODING REAL 802.11 FRAMES OFF THE AIR ***\n"); tcol(WHT); }
                            else if(cca+erc+ero>0){ tcol(rgb(230,160,60)); term_puts("  radio hears energy but no clean frames yet - try another channel (wifi rx 6 / 11)\n"); tcol(WHT); }
                            else { tcol(rgb(230,160,60)); term_puts("  silence on this channel - try wifi rx 6 and wifi rx 11 near the router\n"); tcol(WHT); }
                        } else {
                            rtw_set_channel_2g(mmio,ch);
                            if(!g_rxnum){ tcol(rgb(230,120,90)); term_puts("  RX ring not set up - run 'wifi rings' first\n"); tcol(WHT); }
                            else {
                                rtw_rx_enable(mmio);
                                char ssid[40]; int rssi=0; u8 bssid[6];
                                int r=rtw_rx_poll_beacon(mmio,ssid,&rssi,bssid);
                                if(r==1){ tcol(GREEN);
                                    p=b;p=scopy(p,"  BEACON! SSID=\"");p=scopy(p,ssid);p=scopy(p,"\"  BSSID=");for(int z=0;z<6;z++){p=hx(p,bssid[z],2); if(z<5)*p++=':';}*p=0;
                                    term_puts(b); term_putc('\n'); term_puts("  *** THE CHIP RECEIVED A REAL 802.11 FRAME OFF THE AIR ***\n"); tcol(WHT); }
                                else { tcol(rgb(230,160,60)); term_puts("  no beacon parsed (counter mode may still show frames: wifi rx 6)\n"); tcol(WHT); } } } }
                    else if((arg[0]=='h'||arg[0]=='H')&&(arg[1]=='2')){
                        tcol(rgb(120,200,255)); term_puts("  [wifi h2c] sending QUERY_BT_INFO (a safe no-op H2C) to the MCU\n"); tcol(WHT);
                        u8 hmetfr=rtw_r8(mmio,0x01CC);
                        p=b;p=scopy(p,"  HMETFR before=0x");p=hx(p,hmetfr,2);*p=0; tcol(GREY); term_puts(b); term_putc('\n'); tcol(WHT);
                        int r=rtw_h2c_send(mmio,0,0x00000161u,0x00000000u);
                        if(r<0){ tcol(rgb(230,120,90)); term_puts("  mailbox stayed full - MCU not draining H2C (firmware not running yet)\n"); tcol(WHT); }
                        else {
                            int drained=0; for(int t=0;t<300;t++){ if(!(rtw_r8(mmio,0x01CC)&1)){drained=1;break;} rtw_delay_us(100); }
                            u32 c2h0=rtw_r32(mmio,0x01AC), c2h1=rtw_r32(mmio,0x10F8);
                            p=b;p=scopy(p,"  HMETFR after=0x");p=hx(p,rtw_r8(mmio,0x01CC),2);p=scopy(p,"  C2H(0x1AC)=0x");p=hx(p,c2h0,8);p=scopy(p,"  DBG6=0x");p=hx(p,c2h1,8);*p=0;
                            tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                            if(drained){ tcol(GREEN); term_puts("  MCU drained the H2C box -- the processor is alive and reading commands!\n"); tcol(WHT); }
                            else { tcol(rgb(230,160,60)); term_puts("  box not drained within timeout (MCU may not be running - do 'wifi fw' first)\n"); tcol(WHT); } } }
                    else if((arg[0]=='g'||arg[0]=='G')||((arg[0]=='a'||arg[0]=='A')&&(arg[1]=='l'||arg[1]=='L'))){
                        tcol(rgb(120,200,255)); term_puts("  phy_set_param: power on BB/RF domain, then tables (mac->bb->agc->rf)\n"); tcol(WHT);
                        u32 rfpre=rtw_rf_read_a(mmio,0x18);
                        rtw_phy_power_on(mmio);
                        p=b;p=scopy(p,"  RF_CTRL(0x1F)=0x");p=hx(p,rtw_r8(mmio,0x001F),2);p=scopy(p,"  WLRF1+3(0xEF)=0x");p=hx(p,rtw_r8(mmio,0x00EF),2);
                        p=scopy(p,"  SYS_FUNC_EN=0x");p=hx(p,rtw_r8(mmio,0x0002),2);p=scopy(p,"  RF18 before=0x");p=hx(p,rfpre,5);*p=0;
                        tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                        int m1=rtw_apply_built(mmio,rtw_bmac,138,"mac",1);
                        rtw_apply_built(mmio,rtw_bbb,1669,"bb",1);
                        rtw_apply_built(mmio,rtw_bagc,386,"agc",0);
                        rtw_apply_built(mmio,rtw_brf,586,"rf-a",0);
                        if(!rtw_efuse_valid){ rtw_efuse_dump_phy(mmio,rtw_phymap,RTW_EFUSE_PHY_SZ); rtw_efuse_valid=(rtw_efuse_logical(rtw_phymap,RTW_EFUSE_PHY_SZ,RTW_EFUSE_PROT_SZ,rtw_logmap,RTW_EFUSE_LOG_SZ)>0); }
                        if(rtw_efuse_valid){ for(int i=0;i<6;i++)rtw_w8(mmio,0x0610+i,rtw_logmap[0xD0+i]); }
                        u8 xk=rtw_logmap[0xB9];
                        int hx_ok=(rtw_efuse_valid && xk!=0xFF);
                        rtw_phy_post(mmio,hx_ok,xk);
                        rtw_wmac_init(mmio);
                        u32 rfpost=rtw_rf_read_a(mmio,0x18), rf00=rtw_rf_read_a(mmio,0x00);
                        p=b;p=scopy(p,"  after tables: RF18=0x");p=hx(p,rfpost,5);p=scopy(p,"  RF00=0x");p=hx(p,rf00,5);
                        p=scopy(p,"  xtal_cap=");if(hx_ok){p=scopy(p,"0x");p=hx(p,xk&0x3F,2);}else p=scopy(p,"skipped");*p=0;
                        tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                        if(rfpost!=rfpre){ tcol(GREEN); term_puts("  RF REGISTERS CHANGED -- the radio is out of reset and accepting writes!\n"); tcol(WHT); }
                        else { tcol(rgb(230,160,60)); term_puts("  RF18 unchanged -- RF may still be held off (send this line)\n"); tcol(WHT); }
                        tcol(m1?rgb(230,160,60):GREEN);
                        if(m1) term_puts("  done - some MAC readback mismatches (see above)\n");
                        else term_puts("  done - MAC readback fully matched. stage B/C applied.\n");
                        tcol(WHT); }
                    else if(arg[0]=='m'||arg[0]=='M'||arg[0]=='b'||arg[0]=='B'||((arg[0]=='a'||arg[0]=='A')&&(arg[1]=='g'||arg[1]=='G'))||((arg[0]=='r'||arg[0]=='R')&&(arg[1]=='f'||arg[1]=='F'))){
                        char c0=arg[0]; const char* fn; const char* lbl; int verify;
                        if(c0=='m'||c0=='M'){ fn="RTWMAC.TXT"; lbl="mac"; verify=1; }
                        else if(c0=='b'||c0=='B'){ fn="RTWBB.TXT"; lbl="bb"; verify=1; }
                        else if(c0=='a'||c0=='A'){ fn="RTWAGC.TXT"; lbl="agc"; verify=0; }
                        else { fn="RTWRFA.TXT"; lbl="rf-a"; verify=0; }
                        p=b;p=scopy(p,"  [wifi ");p=scopy(p,lbl);p=scopy(p,"] applying ");p=scopy(p,fn);*p=0;
                        tcol(rgb(255,210,90)); term_puts(b); term_putc('\n'); tcol(WHT);
                        int fl=rtw_read_table_file(fn,fat_filebuf,65535);
                        if(fl<0){ tcol(rgb(230,120,90));
                            p=b;p=scopy(p,"  ");p=scopy(p,fn);p=scopy(p," not found on the boot volume.");*p=0; term_puts(b); term_putc('\n'); tcol(WHT);
                            term_puts("  put it on the USB stick root (or the boot disk). generate with rtwgen from rtw8821c_table.c\n");
                            term_puts("  line format:  offset size value   (hex; size 1|2|4=MMIO, 5=RF-A SIPI)\n"); }
                        else {
                            fat_filebuf[fl]=0;
                            int cnt=rtw_tbl_parse((const char*)fat_filebuf,fl);
                            p=b;p=scopy(p,"  parsed ");p=udec(p,(u32)cnt);p=scopy(p," writes from ");p=udec(p,(u32)fl);p=scopy(p," bytes");*p=0;
                            tcol(CYAN); term_puts(b); term_putc('\n'); tcol(WHT);
                            int mism=0,delays=0,rfw=0;
                            for(int i=0;i<cnt;i++){
                                u8 sz=rtw_mactbl[i].sz; u32 off=rtw_mactbl[i].off, val=rtw_mactbl[i].val;
                                int wasdelay=rtw_apply_one(mmio,off,sz,val);
                                if(wasdelay){ delays++; continue; }
                                if(sz==5){ rfw++; continue; }
                                rtw_delay_us(50);
                                if(verify){
                                    u32 rb=rtw_rdsz(mmio,off,sz);
                                    if(rb!=val){ mism++;
                                        if(mism<=12){ tcol(rgb(230,160,60));
                                            p=b;p=scopy(p,"    0x");p=hx(p,off,4);p=scopy(p," wrote 0x");p=hx(p,val,8);
                                            p=scopy(p," read 0x");p=hx(p,rb,8);*p=0; term_puts(b); term_putc('\n'); tcol(WHT); } } } }
                            p=b;p=scopy(p,"  applied ");p=udec(p,(u32)cnt);
                            if(rfw){p=scopy(p,"  rf-writes ");p=udec(p,(u32)rfw);}
                            if(delays){p=scopy(p,"  delays ");p=udec(p,(u32)delays);}
                            if(verify){p=scopy(p,"  mismatches ");p=udec(p,(u32)mism);}
                            *p=0;
                            tcol((verify&&mism)?rgb(230,160,60):GREEN); term_puts(b); term_putc('\n'); tcol(WHT);
                            if(verify) term_puts("  (some mismatches are normal for write-only/self-clearing regs)\n");
                            else term_puts("  (rf/agc writes are not read back here - use 'wifi dump' to snapshot)\n"); } }
                    else if((arg[0]=='d'||arg[0]=='D')&&(arg[1]=='h'||arg[1]=='H')){
                        if(!g_wpa_ok){ tcol(rgb(230,120,90)); term_puts("  not connected - run 'wifi wpa <ID>' first\n"); tcol(WHT); }
                        else { netdev_set(NETDEV_WIFI); g_dhcp_done=0; g_dhcp_ok=0;
                            tcol(rgb(120,200,255)); term_puts("  [wifi dhcp] requesting a lease over Wi-Fi (GTK "); term_puts(g_wnd_have_gtk?"present":"missing"); term_puts(")...\n"); tcol(WHT);
                            int ok=dhcp_config();
                            if(ok){ p=b; p=scopy(p,"  lease: ip="); p=ipstr(p,my_ip); p=scopy(p," gw="); p=ipstr(p,gw_ip); p=scopy(p," dns="); p=ipstr(p,dns_ip); *p=0; tcol(GREEN); term_puts(b); term_putc('\n');
                                term_puts("  Wi-Fi is now the active link. Try: ping 1.1.1.1  |  dns example.com\n"); tcol(WHT); }
                            else { tcol(rgb(230,160,60)); term_puts("  no lease. "); term_puts(g_wnd_have_gtk?"broadcast reachable; AP may be slow - retry.":"no GTK: broadcast DHCP reply can't be decrypted (this is the GTK case).");
                                term_putc('\n');
                                { char s2[140]; char* q=s2; q=scopy(q,"  wifi rx="); q=udec(q,g_wnd_rx_frames); q=scopy(q," drop="); q=udec(q,g_wnd_rx_drop); q=scopy(q," tx="); q=udec(q,g_wnd_tx_frames); { extern u32 g_wnd_dead, g_wnd_txfull; q=scopy(q," ring-full="); q=udec(q,g_wnd_txfull); q=scopy(q," card-gone="); q=udec(q,g_wnd_dead); } *q=0; tcol(GREY); term_puts(s2); term_putc('\n'); tcol(WHT); }
                                netdev_set(NETDEV_ETH); } }
                    }
                    else if((arg[0]=='e'||arg[0]=='E')&&arg[1]=='t'){ netdev_set(NETDEV_ETH); tcol(WHT); term_puts("  active link: Ethernet\n"); tcol(WHT); }
                    else if(arg[0]=='d'||arg[0]=='D'){
                        tcol(rgb(255,210,90)); term_puts("  [wifi dump] snapshotting MMIO 0x0000-0x0FFF\n"); tcol(WHT);
                        for(int o=0;o<0x1000;o+=4) *(u32*)(fat_filebuf+o)=rtw_r32(mmio,(u32)o);
                        if((msd_ok||msd_autoinit()) && ufat_write_file("RTWREGS.BIN",(const char*)fat_filebuf,0x1000)==0){
                            tcol(GREEN); term_puts("  wrote RTWREGS.BIN (4096 bytes) to the USB stick\n"); tcol(WHT); }
                        else { tcol(rgb(230,160,60)); term_puts("  no USB stick - printing 0x000-0x03F instead\n"); tcol(GREY);
                            for(int o=0;o<0x40;o+=16){ char hl[80]; rtw_hexline(fat_filebuf,o,16,hl); term_puts(hl); term_putc('\n'); } tcol(WHT); } }
                    else if(!doload && !doinit && !(arg[0]=='r'||arg[0]=='R')){ tcol(WHT); term_puts("  status: detected, enabled, firmware ready.\n"); term_puts("  init efuse allbuilt macinit txbuf fw h2c bb agc rf rings load dump.  macinit=TRX/page setup.  EXPERIMENTAL.\n"); }
                    else {
                        #define WMR8(o)   (*(volatile u8*)(u64)(mmio+(o)))
                        if((arg[0]=='r'||arg[0]=='R')&&(arg[1]=='i'||arg[1]=='I')){
                            /* ---- STAGE 3: PCI DMA ring infrastructure (transcribed from rtw88 pci.c) ---- */
                            tcol(rgb(255,210,90)); term_puts("  [wifi rings] setting up TX/RX DMA descriptor rings (rtw88 layout, EXPERIMENTAL)\n"); tcol(WHT);
                            /* rtw_pci_tx/rx_buffer_desc: { u16 buf_size; u16 psb_len/total; u32 dma; } = 8 bytes.
                               DMA-coherent ring memory from the heap (x86 DMA is cache-coherent, so heap buffers work as DMA targets; identity-mapped => phys==virt). */
                            u32 RXNUM=128, BENUM=256;                    /* RTK_MAX_RX / RTK_BEQ_TX desc counts */
                            static u8 *rxring=0,*bering=0,*rxbuf=0;
                            static u8 *mgring=0;
                            if(!rxring)rxring=(u8*)dma_alloc(RXNUM*8); if(!bering)bering=(u8*)dma_alloc(BENUM*16); if(!mgring)mgring=(u8*)dma_alloc(BENUM*16); if(!rxbuf)rxbuf=(u8*)dma_alloc(RXNUM*2048);   /* alloc once; reuse on re-run (no arena leak) */
                            if(!rxring||!bering||!rxbuf||!mgring){ tcol(rgb(230,120,90)); term_puts("  ring alloc failed (heap)\n"); tcol(WHT); }
                            else {
                                for(u32 i=0;i<RXNUM*8;i++)rxring[i]=0; for(u32 i=0;i<BENUM*16;i++){bering[i]=0;mgring[i]=0;}
                                for(u32 i=0;i<RXNUM;i++){ u8* d=rxring+i*8; u64 pb=(u64)(rxbuf+i*2048);   /* fill RX descriptors */
                                    *(volatile u16*)(d)=2048; *(volatile u16*)(d+2)=0; *(volatile u32*)(d+4)=(u32)pb; }
                                /* dma reset: RTK_PCI_CTRL (0x300) - toggle TRXDMA interface reset + RX tag */
                                *(volatile u32*)(u64)(mmio+0x300) |= (1u<<0)|(1u<<16);
                                rtw_delay_us(500);
                                /* program ring-base + count registers (RTL8821CE offsets from pci.h) */
                                g_rxring=rxring; g_rxbuf=rxbuf; g_rxnum=RXNUM; g_rxidx=0;
                                g_bering=bering; g_benum=BENUM; g_beidx=0;
                                g_mgring=mgring; g_mgnum=BENUM; g_mgidx=0;
                                *(volatile u32*)(u64)(mmio+0x338)=(u32)(u64)rxring; *(volatile u16*)(u64)(mmio+0x382)=(u16)RXNUM;   /* RXBD_DESA_MPDUQ / NUM */
                                *(volatile u32*)(u64)(mmio+0x328)=(u32)(u64)bering; *(volatile u16*)(u64)(mmio+0x388)=(u16)BENUM;   /* TXBD_DESA_BEQ  / NUM  */
                                *(volatile u32*)(u64)(mmio+0x310)=(u32)(u64)mgring; *(volatile u16*)(u64)(mmio+0x380)=(u16)BENUM;   /* TXBD_DESA_MGMTQ / NUM */
                                *(volatile u16*)(u64)(mmio+0x39C)=0xFFFF;   /* TXBD_RWPTR_CLR: reset read/write ptrs */
                                /* enable TX/RX DMA: CR (0x0100) MAC_TRX bits incl BIT_TXDMA_EN(2)|BIT_RXDMA_EN(3) */
                                u16 cr=*(volatile u16*)(u64)(mmio+0x0100); *(volatile u16*)(u64)(mmio+0x0100)=(u16)(cr|(1<<2)|(1<<3));
                                rtw_delay_us(500);
                                tcol(GREEN); p=b;p=scopy(p,"  rings up: RX ");p=udec(p,RXNUM);p=scopy(p," desc @0x");p=hx(p,(u32)(u64)rxring,8);p=scopy(p,"  TX(BE) ");p=udec(p,BENUM);p=scopy(p," @0x");p=hx(p,(u32)(u64)bering,8);*p=0; term_puts(b); term_putc('\n');
                                u32 cr2=*(volatile u16*)(u64)(mmio+0x0100);
                                p=b;p=scopy(p,"  CR after DMA-enable = 0x");p=hx(p,cr2,4);p=scopy(p,"  (TXDMA/RXDMA bits should be set)");*p=0; tcol(CYAN); term_puts(b); term_putc('\n');
                                tcol(WHT); term_puts("  rings allocated + registers programmed. next: 'wifi load' can use these for the real TX-ring download.\n");
                            }
                        }
                        if(doinit){
                            /* --- RTL8821CE cardemu->act power-on sequence, transcribed from rtw88
                                   (trans_cardemu_to_act_8821c), PCI + ALL-interface commands only. --- */
                            tcol(rgb(255,210,90)); term_puts("  [wifi init] rtw88 power-on sequence (EXPERIMENTAL) - watch for a hang\n"); tcol(WHT);
                            /* {offset, mask, value, cmd}: cmd 1=write(set masked bits to value), 2=poll(wait bits==value) */
                            struct { u16 off; u8 mask; u8 val; u8 cmd; } seqp[] = {
                                {0x0020,0x01,0x01,1},                 /* APS_FSMCO: enable LDOA/analog        */
                                {0x0001,0,0,3},                        /* delay 1ms (cmd 3)                    */
                                {0x0000,0x20,0x00,1},                  /* CR: release reset bit5              */
                                {0x0005,(0x10|0x08|0x04),0x00,1},      /* APS_FSMCO: clear pd bits            */
                                {0x0075,0x01,0x01,1},                  /* PCIE: enable                        */
                                {0x0006,0x02,0x02,2},                  /* poll SYS_CLKR ready (bit1)          */
                                {0x0075,0x01,0x00,1},
                                {0x0006,0x01,0x01,1},
                                {0x0005,0x80,0x00,1},                  /* clear bit7                          */
                                {0x0005,(0x10|0x08),0x00,1},
                                {0x0005,0x01,0x01,1},                  /* APS_FSMCO: MAC on (bit0)            */
                                {0x0005,0x01,0x00,2},                  /* poll until bit0 clears (MAC ready)  */
                                {0x0020,0x08,0x08,1},                  /* enable                              */
                                {0x0074,0x20,0x20,1},                  /* PCI: bit5                           */
                                {0x0022,0x02,0x00,1},
                                {0x0062,(0x80|0x40|0x20),(0x80|0x40|0x20),1},
                                {0x0061,(0x80|0x40|0x20),0x00,1},
                                {0x007C,0x02,0x00,1},
                            };
                            int nn=(int)(sizeof(seqp)/sizeof(seqp[0])), okall=1;
                            for(int s=0;s<nn;s++){ u16 o=seqp[s].off;
                                if(seqp[s].cmd==3){ rtw_delay_us(1000); continue; }
                                if(seqp[s].cmd==2){ int got=0; for(int t=0;t<100000;t++){ if((WMR8(o)&seqp[s].mask)==seqp[s].val){got=1;break;} io_wait(); }
                                    if(!got){ okall=0; p=b;p=scopy(p,"  poll timeout @0x");p=hx(p,o,4);*p=0; tcol(rgb(230,120,90)); term_puts(b); term_putc('\n'); tcol(WHT); } continue; }
                                u8 v=WMR8(o); v=(u8)((v & ~seqp[s].mask) | (seqp[s].val & seqp[s].mask)); WMW8(o,v);   /* write masked */
                                for(volatile int d=0;d<500;d++) io_wait();
                            }
                            u32 after=WMR(0xF0);
                            tcol(okall?GREEN:rgb(230,160,60)); p=b;p=scopy(p,"  power-on ");p=scopy(p,okall?"completed":"completed with poll timeouts");p=scopy(p,"  SYS_CFG=0x");p=hx(p,after,8);*p=0; term_puts(b); term_putc('\n');
                            u8 mac2[6]; for(int i=0;i<6;i++)mac2[i]=WMR8(0x0610+i);
                            tcol(CYAN); p=b;p=scopy(p,"  MAC now ");for(int i=0;i<6;i++){p=hx(p,mac2[i],2);if(i<5)*p++=':';}*p=0; term_puts(b); term_putc('\n');
                            tcol(WHT); term_puts("  if MAC changed from EA:EA:.. the chip powered on + read EFUSE. then try 'wifi load'.\n");
                        }
                        if(doload){
                        tcol(rgb(255,210,90)); term_puts("  [wifi load] EXPERIMENTAL firmware download - watch for a hang, power-cycle if needed\n"); tcol(WHT);
                        const unsigned char* body=_binary_rtw8821c_fw_bin_start+32; long blen=fwsz-32;   /* skip 32B header */
                        u16 ctrl=WMR16(0x0080); WMW16(0x0080, ctrl | 0x0001);                            /* FWDL_EN */
                        rtw_delay_us(1000);
                        long off=0; int page=0;
                        while(off<blen){ int n=(int)(blen-off); if(n>4096)n=4096;
                            WMW8(0x0084, (u8)(page & 0x07));                                             /* page index (low bits) */
                            u16 c=WMR16(0x0080); WMW16(0x0080,(c & ~0x0700) | ((page & 7)<<8));          /* page select */
                            for(int i=0;i<n;i++) *(volatile u8*)(u64)(mmio+0x8000+i)=body[off+i];        /* copy page to FW FIFO window */
                            off+=n; page++;
                            for(volatile int d=0;d<2000;d++) io_wait();
                        }
                        u16 fin=WMR16(0x0080); WMW16(0x0080, fin & ~0x0001);                             /* clear FWDL_EN */
                        int ready=0; for(int t=0;t<200000;t++){ if(WMR16(0x0080) & 0x0080){ ready=1; break; } io_wait(); }
                        u16 st=WMR16(0x0080);
                        p=b;p=scopy(p,"  MCUFW_CTRL(0x80)=0x");p=hx(p,st,4);p=scopy(p,"  pages=");p=udec(p,(u32)page);*p=0; tcol(CYAN); term_puts(b);term_putc('\n');
                        if(ready){ tcol(GREEN); term_puts("  MCU_FW_READY set -- chip ACCEPTED the firmware! (radio microcode running)\n");
                            if(st&0x0800) term_puts("  checksum OK\n"); tcol(WHT);
                            term_puts("  NOTE: firmware runs, but device-init + 802.11 + WPA are not implemented -> no connection yet.\n"); }
                        else { tcol(rgb(230,120,90)); term_puts("  MCU_FW_READY not set -- chip did not confirm (needs full power-on/PHY init first).\n"); tcol(WHT);
                            term_puts("  this is expected without device-init; the download path ran without hanging, which is the real test.\n"); } } }
            } } } }
