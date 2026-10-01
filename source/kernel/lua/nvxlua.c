#include "src/lua.h"
#include "src/lauxlib.h"
#include "src/lualib.h"
extern unsigned long strlen(const char*);
extern void nvxlc_window(const char*,int,int);
extern void nvxlc_clear(unsigned int);
extern void nvxlc_rect(int,int,int,int,unsigned int);
extern void nvxlc_pixel(int,int,unsigned int);
extern void nvxlc_text(int,int,const char*,unsigned int);
extern void nvxlc_line(int,int,int,int,unsigned int);
extern void nvxlc_present(void);
extern int  nvxlc_w(void); extern int nvxlc_h(void);
extern int  nvxlc_key(void); extern int nvxlc_waitkey(void);
extern void nvxlc_mouse(int*,int*,int*);
extern unsigned long nvxlc_ticks(void); extern void nvxlc_sleep(int);

static char g_lua_out[16384]; static int g_lua_out_n=0;
void lua_emit(const char* s,int n){ for(int i=0;i<n && g_lua_out_n<16383;i++)g_lua_out[g_lua_out_n++]=s[i]; g_lua_out[g_lua_out_n]=0; }

static int lb_window(lua_State*L){ nvxlc_window(luaL_checkstring(L,1),(int)luaL_checkinteger(L,2),(int)luaL_checkinteger(L,3)); return 0; }
static int lb_clear(lua_State*L){ nvxlc_clear((unsigned int)luaL_checkinteger(L,1)); return 0; }
static int lb_rect(lua_State*L){ nvxlc_rect((int)luaL_checkinteger(L,1),(int)luaL_checkinteger(L,2),(int)luaL_checkinteger(L,3),(int)luaL_checkinteger(L,4),(unsigned int)luaL_checkinteger(L,5)); return 0; }
static int lb_pixel(lua_State*L){ nvxlc_pixel((int)luaL_checkinteger(L,1),(int)luaL_checkinteger(L,2),(unsigned int)luaL_checkinteger(L,3)); return 0; }
static int lb_text(lua_State*L){ nvxlc_text((int)luaL_checkinteger(L,1),(int)luaL_checkinteger(L,2),luaL_checkstring(L,3),(unsigned int)luaL_checkinteger(L,4)); return 0; }
static int lb_line(lua_State*L){ nvxlc_line((int)luaL_checkinteger(L,1),(int)luaL_checkinteger(L,2),(int)luaL_checkinteger(L,3),(int)luaL_checkinteger(L,4),(unsigned int)luaL_checkinteger(L,5)); return 0; }
static int lb_rgb(lua_State*L){ int r=(int)luaL_checkinteger(L,1),g=(int)luaL_checkinteger(L,2),b=(int)luaL_checkinteger(L,3); lua_pushinteger(L,((unsigned)r<<16)|((unsigned)g<<8)|(unsigned)b); return 1; }
static int lb_present(lua_State*L){ (void)L; nvxlc_present(); return 0; }
static int lb_width(lua_State*L){ lua_pushinteger(L,nvxlc_w()); return 1; }
static int lb_height(lua_State*L){ lua_pushinteger(L,nvxlc_h()); return 1; }
static int lb_key(lua_State*L){ lua_pushinteger(L,nvxlc_key()); return 1; }
static int lb_waitkey(lua_State*L){ lua_pushinteger(L,nvxlc_waitkey()); return 1; }
static int lb_mouse(lua_State*L){ int x,y,b; nvxlc_mouse(&x,&y,&b); lua_pushinteger(L,x); lua_pushinteger(L,y); lua_pushinteger(L,b); return 3; }
static int lb_ticks(lua_State*L){ lua_pushinteger(L,(lua_Integer)nvxlc_ticks()); return 1; }
static int lb_sleep(lua_State*L){ nvxlc_sleep((int)luaL_checkinteger(L,1)); return 0; }

static const luaL_Reg nvxlib[]={
    {"window",lb_window},{"clear",lb_clear},{"rect",lb_rect},{"pixel",lb_pixel},
    {"text",lb_text},{"line",lb_line},{"rgb",lb_rgb},{"present",lb_present},
    {"width",lb_width},{"height",lb_height},{"key",lb_key},{"waitkey",lb_waitkey},
    {"mouse",lb_mouse},{"ticks",lb_ticks},{"sleep",lb_sleep},{0,0}
};

const char* nvx_lua_run(const char* code){
    g_lua_out_n=0; g_lua_out[0]=0;
    lua_State* L=luaL_newstate();
    if(!L){ return "lua: cannot create state"; }
    luaL_openlibs(L);
    luaL_newlib(L,nvxlib); lua_setglobal(L,"nvx");
    if(luaL_dostring(L,code)!=LUA_OK){
        const char* e=lua_tostring(L,-1);
        lua_emit("error: ",7); lua_emit(e?e:"(unknown)", e?(int)strlen(e):9); lua_emit("\n",1);
    }
    lua_close(L);
    return g_lua_out;
}
