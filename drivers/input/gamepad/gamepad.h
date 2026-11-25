#ifndef NVX_GAMEPAD_H
#define NVX_GAMEPAD_H
#define GP_MAX 4
#define GP_Q 512
#define GP_NKEY 13
#define GP_NABS 8
#define GPC_RUMBLE 1
#define GPC_LED 2
#define GPC_TOUCH 4
#define GPC_BAT 8
struct gpev { u64 ns; u16 type, code; int val; };
struct gpad { int used, type, bus, player, caps, ref, seen, bat, chg, rs, rw, tp_up; u16 vid, pid; u32 gen, nrep; u64 rstop; u8 rgb[3]; char name[48];
    int (*out)(int idx,int ref,int strong,int weak,u8 r,u8 g,u8 b); struct gpst st; volatile u32 qw; struct gpev q[GP_Q]; };
extern const u16 gp_keylist[GP_NKEY];
extern const u8 gp_abslist[GP_NABS];
extern u32 g_gp_gen;
extern int g_gp_tpptr;
int gpad_valid(int i);
struct gpad* gpad_at(int i);
int gpad_count(void);
int gpad_add(int type,int bus,u16 vid,u16 pid,const char* name,int caps,int (*out)(int,int,int,int,u8,u8,u8),int ref);
void gpad_remove(int i);
void gpad_report(int i,const struct gpst* s);
int gpad_set_out(int i,int strong,int weak,int r,int g,int b);
int gpad_rumble(int i,int strong,int weak,int ms);
int gpad_led(int i,int r,int g,int b);
void gpad_tick(void);
int gpad_get(int i,struct gpst* s);
const char* gpad_typename(int t);
int gpad_read_ev(int i,u32* pos,struct gpev* out,int max,int* dropped);
u32 gpad_qpos(int i);
int gpad_absinfo(int code,int* lo,int* hi,int* flat);
int gpad_absval(int i,int code);
int gpad_keyval(int i,int code);
int gpad_info(char* out,int cap);
void gpad_pointer(int dx,int dy,int click);
#endif
