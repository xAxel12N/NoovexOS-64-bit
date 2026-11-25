#include "include/nvx/sys/bpf.h"
#include "include/nvx/sys/bpf/bpf_attach.h"
static int lxb_get(int fd,struct bpf_map** m,struct bpf_prog** p){ *m=0; *p=0; struct vfile* v=lx_vf(fd); if(!v||v->dev!=LX_BPFD)return -1; return bpf_slot_get(v->fs_idx,m,p)?-1:0; }
struct bpf_prog* bpf_prog_by_fd(int fd){ struct bpf_map* m; struct bpf_prog* p; if(lxb_get(fd,&m,&p))return 0; if(m)bpf_map_put(m); return p; }
struct bpf_map* bpf_map_by_fd(int fd){ struct bpf_map* m; struct bpf_prog* p; if(lxb_get(fd,&m,&p))return 0; if(p)bpf_prog_put(p); return m; }
static long lxb_fd(struct bpf_map* m,struct bpf_prog* p){
    int s=bpf_slot_new(m,p); if(s<0)return -LX_EMFILE;
    int vi=jc_newvf(LX_BPFD,s,O_RDWR); if(vi<0){ bpf_fd_close(s); return -LX_EMFILE; }
    int fd=lx_fd_install(vi,1,0); if(fd<0)vf_unref(vi); return fd;
}
static long lxb_map_op(int cmd,union bpf_attr* a){
    struct bpf_map* m=bpf_map_by_fd((int)a->me.map_fd); if(!m)return -LX_EBADF;
    long r=0; u8 key[512]; u8* val=0; u32 ks=m->key_size, vb=m->type==BPF_MAP_TYPE_PERCPU_ARRAY?m->elem_size*BPF_NCPU:m->value_size;
    if(m->type==BPF_MAP_TYPE_RINGBUF){
        if(cmd!=BPF_MAP_LOOKUP_ELEM||!a->me.value||a->me.flags==0||a->me.flags>65536){ r=-LX_EINVAL; goto out; }
        val=(u8*)kmalloc(a->me.flags); if(!val){ r=-12; goto out; }
        r=bpf_ringbuf_read(m,val,(u32)a->me.flags); if(r==0)r=-2; else if(r>0&&lx_put(a->me.value,val,(u64)r))r=-LX_EFAULT;
        goto out;
    }
    if(a->me.key&&lx_get(key,a->me.key,ks)){ r=-LX_EFAULT; goto out; }
    if(!a->me.key&&cmd!=BPF_MAP_GET_NEXT_KEY){ r=-LX_EINVAL; goto out; }
    if(cmd==BPF_MAP_GET_NEXT_KEY){ u8 nk[512]; r=bpf_map_next_key(m,a->me.key?key:0,nk); if(!r&&lx_put(a->me.value,nk,ks))r=-LX_EFAULT; goto out; }
    if(cmd==BPF_MAP_DELETE_ELEM){ r=bpf_map_delete(m,key); goto out; }
    val=(u8*)kcalloc(1,vb); if(!val){ r=-12; goto out; }
    if(cmd==BPF_MAP_LOOKUP_ELEM){
        if(m->type==BPF_MAP_TYPE_PERCPU_ARRAY){ u32 k=*(u32*)key; if(k>=m->max_entries){ r=-2; goto out; } for(u32 c=0;c<BPF_NCPU;c++){ u8* s=m->data+((u64)c*m->max_entries+k)*m->elem_size; for(u32 i=0;i<m->value_size;i++)val[c*m->elem_size+i]=s[i]; } }
        else { u8* p=(u8*)bpf_map_lookup(m,key); if(!p){ r=-2; goto out; } for(u32 i=0;i<vb;i++)val[i]=p[i]; }
        if(lx_put(a->me.value,val,vb))r=-LX_EFAULT;
        goto out;
    }
    if(cmd==BPF_MAP_UPDATE_ELEM){
        if(lx_get(val,a->me.value,vb)){ r=-LX_EFAULT; goto out; }
        if(m->type==BPF_MAP_TYPE_PERCPU_ARRAY){ u32 k=*(u32*)key; if(a->me.flags>BPF_EXIST){ r=-LX_EINVAL; goto out; } if(a->me.flags==BPF_NOEXIST){ r=-17; goto out; } if(k>=m->max_entries){ r=-7; goto out; } for(u32 c=0;c<BPF_NCPU;c++){ u8* d=m->data+((u64)c*m->max_entries+k)*m->elem_size; for(u32 i=0;i<m->value_size;i++)d[i]=val[c*m->elem_size+i]; } }
        else r=bpf_map_update(m,key,val,a->me.flags);
        goto out;
    }
    r=-LX_EINVAL;
out: if(val)kfree(val); bpf_map_put(m); return r;
}
static long lxb_prog_load(union bpf_attr* a){
    u32 n=a->pl.insn_cnt; if(n==0||n>BPF_MAXINSNS||!a->pl.insns)return -LX_EINVAL;
    struct bpf_insn* in=(struct bpf_insn*)kmalloc((u64)n*sizeof *in); if(!in)return -12;
    struct bpf_map* maps[BPF_PROG_MAPS]; int nm=0; long r=0; char* log=0; u32 lsz=a->pl.log_size;
    if(lx_get(in,a->pl.insns,(u64)n*sizeof *in)){ r=-LX_EFAULT; goto out; }
    for(u32 i=0;i<n;i++){
        if(in[i].code!=(BPF_LD|BPF_DW|BPF_IMM)||!in[i].src)continue;
        if(in[i].src!=BPF_PSEUDO_MAP_FD){ r=-LX_EINVAL; goto out; }
        struct bpf_map* m=bpf_map_by_fd(in[i].imm); if(!m){ r=-LX_EBADF; goto out; }
        int k=0; while(k<nm&&maps[k]!=m)k++;
        if(k<nm)bpf_map_put(m); else { if(nm>=BPF_PROG_MAPS){ bpf_map_put(m); r=-LX_E2BIG; goto out; } maps[nm++]=m; }
        in[i].src=BPF_PSEUDO_MAP_IDX; in[i].imm=k; i++;
    }
    if(a->pl.log_level&&a->pl.log_buf&&lsz){ if(lsz>65536)lsz=65536; log=(char*)kcalloc(1,lsz); }
    { int err=0; struct bpf_prog* p=bpf_prog_new(a->pl.prog_type,in,n,maps,nm,log,log?lsz:0,&err);
      if(!p)r=err; else { for(int k=0;k<16;k++)p->name[k]=a->pl.prog_name[k]; p->name[15]=0; p->uid=(u32)g_jp[g_jcur].uid; r=lxb_fd(0,p); bpf_prog_put(p); } }
    if(log){ u32 l=0; while(l+1<lsz&&log[l])l++; lx_put(a->pl.log_buf,log,l+1); }
out: for(int k=0;k<nm;k++)bpf_map_put(maps[k]); if(log)kfree(log); kfree(in); return r;
}
static long lxb_info(union bpf_attr* a){
    struct bpf_map* m; struct bpf_prog* p; if(lxb_get((int)a->info.bpf_fd,&m,&p))return -LX_EBADF;
    u8 b[sizeof(struct bpf_prog_info_out)]; u32 n=0;
    for(u32 i=0;i<sizeof b;i++)b[i]=0;
    if(m){ struct bpf_map_info_out* o=(struct bpf_map_info_out*)b; o->type=m->type; o->id=(u32)m->id; o->key_size=m->key_size; o->value_size=m->value_size; o->max_entries=m->max_entries; o->map_flags=m->flags; for(int k=0;k<15;k++)o->name[k]=m->name[k]; n=sizeof *o; }
    else if(p){ struct bpf_prog_info_out* o=(struct bpf_prog_info_out*)b; o->type=p->type; o->id=(u32)p->id; o->xlated_prog_len=p->len*8; o->created_by_uid=p->uid; o->nr_map_ids=(u32)p->nmaps; for(int k=0;k<15;k++)o->name[k]=p->name[k]; n=sizeof *o; }
    if(m)bpf_map_put(m);
    if(p)bpf_prog_put(p);
    if(!n)return -LX_EBADF;
    if(a->info.info_len<n)n=a->info.info_len;
    if(n&&lx_put(a->info.info,b,n))return -LX_EFAULT;
    a->info.info_len=n; return 0;
}
static long lxb_rtp_open(union bpf_attr* a){
    char nm[64]; u32 i=0; if(!a->rtp.name)return -LX_EINVAL;
    for(;i<sizeof nm;i++){ if(lx_get(&nm[i],a->rtp.name+i,1))return -LX_EFAULT; if(!nm[i])break; }
    if(i>=sizeof nm)return -LX_EINVAL;
    int tp=bpf_tp_by_name(nm); if(tp<0)return -2;
    struct bpf_prog* p=bpf_prog_by_fd((int)a->rtp.prog_fd); if(!p)return -LX_EBADF;
    const struct bpf_ctx_desc* cd=bpf_ctx_get(p->type); long r;
    if((p->type!=BPF_PROG_TYPE_RAW_TRACEPOINT&&p->type!=BPF_PROG_TYPE_TRACEPOINT)||!cd||cd->size>sizeof(struct bpf_trace_ctx)+64){ bpf_prog_put(p); return -LX_EINVAL; }
    int s=bpf_slot_new(0,p); if(s<0){ bpf_prog_put(p); return -LX_EMFILE; }
    int id=bpf_tp_attach(tp,p); bpf_prog_put(p);
    if(id<0){ bpf_fd_close(s); return id; }
    if(bpf_slot_set_link(s,id)){ bpf_tp_detach_id(id); bpf_fd_close(s); return -LX_EINVAL; }
    int vi=jc_newvf(LX_BPFD,s,O_RDWR); if(vi<0){ bpf_fd_close(s); return -LX_EMFILE; }
    r=lx_fd_install(vi,1,0); if(r<0)vf_unref(vi); return r;
}
static long lxm_bpf(int cmd,u64 uattr,u32 size){
    if(!bpf_allowed())return -LX_EPERM;
    bpf_attach_init();
    union bpf_attr a; for(u32 i=0;i<sizeof a;i++)a.raw[i]=0;
    if(!uattr)return -LX_EFAULT;
    if(size>4096)return -LX_E2BIG;
    u32 cp=size<sizeof a?size:(u32)sizeof a;
    if(lx_get(&a,uattr,cp))return -LX_EFAULT;
    for(u32 i=sizeof a;i<size;i++){ u8 z; if(lx_get(&z,uattr+i,1))return -LX_EFAULT; if(z)return -LX_E2BIG; }
    switch(cmd){
    case BPF_MAP_CREATE: { int err=0; struct bpf_map* m=bpf_map_new(a.mc.map_type,a.mc.key_size,a.mc.value_size,a.mc.max_entries,a.mc.map_flags,&err); if(!m)return err;
        for(int k=0;k<15;k++)m->name[k]=a.mc.map_name[k];
        long fd=lxb_fd(m,0); bpf_map_put(m); return fd; }
    case BPF_MAP_LOOKUP_ELEM: case BPF_MAP_UPDATE_ELEM: case BPF_MAP_DELETE_ELEM: case BPF_MAP_GET_NEXT_KEY: return lxb_map_op(cmd,&a);
    case BPF_PROG_LOAD: return lxb_prog_load(&a);
    case BPF_RAW_TRACEPOINT_OPEN: return lxb_rtp_open(&a);
    case BPF_OBJ_GET_INFO_BY_FD: { long r=lxb_info(&a); if(!r&&lx_put(uattr+__builtin_offsetof(union bpf_attr,info.info_len),&a.info.info_len,4))r=-LX_EFAULT; return r; }
    }
    return -LX_EINVAL;
}
