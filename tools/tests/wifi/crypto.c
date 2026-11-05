#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lib/crypto/nvxcrypto.c"
#include "lib/crypto/nvxtls.c"
#include "lib/crypto/nvxcrypto2.c"
#include "lib/crypto/nvxx509.c"
#include "lib/crypto/nvxroots.c"
#include "lib/crypto/nvxwpa.c"
u8 g_ptk[48];
#include "net/wifi/wpa3.c"
#include "net/wifi/eap.c"
void* kmalloc(unsigned long n){ return malloc(n); }
void kfree(void* p){ free(p); }
void tls_prepare(void){}
i64 tls_clock_now(void){ return 0; }
static int hexb(const char* h,u8* o){ int n=0; while(h[0]&&h[1]){ unsigned v; sscanf(h,"%2x",&v); o[n++]=(u8)v; h+=2; } return n; }
static int fails=0, passes=0;
static void ck(int c,const char* t){ if(c){ passes++; printf("PASS %s\n",t); } else { fails++; printf("FAIL %s\n",t); } }
int main(void){
    u8 k[16],m[64],e[16],o[16]; hexb("2b7e151628aed2a6abf7158809cf4f3c",k);
    hexb("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710",m);
    aes_cmac(k,m,0,o); hexb("bb1d6929e95937287fa37d129b756746",e); ck(!memcmp(o,e,16),"AES-CMAC empty (RFC 4493)");
    aes_cmac(k,m,16,o); hexb("070a16b46b4d4144f79bdd9dd04a287c",e); ck(!memcmp(o,e,16),"AES-CMAC 16 bytes");
    aes_cmac(k,m,40,o); hexb("dfa66747de9ae63030ca32611497c827",e); ck(!memcmp(o,e,16),"AES-CMAC 40 bytes");
    aes_cmac(k,m,64,o); hexb("51f0bebf7e3b9d92fc49741779363cfe",e); ck(!memcmp(o,e,16),"AES-CMAC 64 bytes");
    u8 one[32]={0}; one[31]=1; u8 G[65],G2[65],G2b[65],two[32]={0}; two[31]=2;
    ck(p256_mul(one,0,G)==0&&G[0]==4,"P-256 1*G");
    p256_mul(two,0,G2); p256_add(G,G,G2b); ck(!memcmp(G2,G2b,65),"P-256 2*G == G+G");
    u8 y[32]; ck(p256_y_from_x(G+1,y)&&(!memcmp(y,G+33,32)||1),"P-256 y from x exists for G");
    { u8 p[32]; p256_prime(p); u8 ny[65]; memcpy(ny,G,65); p256_neg(ny); u8 s[65]; ck(p256_add(G,ny,s)<0,"P-256 G + (-G) = infinity"); ck(!memcmp(y,G+33,32)||!memcmp(y,ny+33,32),"P-256 sqrt matches +-y"); }
    u8 a1[6]={0x4d,0x3f,0x2f,0xff,0xe3,0x87}, a2[6]={0xa5,0xd8,0xaa,0x95,0x8e,0x3c};
    sae_ctx A,B; memset(&A,0,sizeof A); memset(&B,0,sizeof B);
    ck(sae_commit(&A,a1,a2,"mekmitasdigoat")==0,"SAE commit A"); ck(sae_commit(&B,a2,a1,"mekmitasdigoat")==0,"SAE commit B");
    ck(!memcmp(A.pwe,B.pwe,65),"SAE both sides derive the same PWE");
    ck(sae_process_commit(&A,B.scalar,B.elem+1)==0,"SAE A processes B"); ck(sae_process_commit(&B,A.scalar,A.elem+1)==0,"SAE B processes A");
    ck(!memcmp(A.pmk,B.pmk,32)&&!memcmp(A.kck,B.kck,32),"SAE same PMK and KCK");
    u8 ca[32],cb[32]; sae_confirm(&A,1,ca); sae_confirm(&B,1,cb); ck(sae_check_confirm(&B,1,ca)==0&&sae_check_confirm(&A,1,cb)==0,"SAE confirms verify");
    sae_ctx C; memset(&C,0,sizeof C); sae_commit(&C,a2,a1,"wrongpassword"); sae_ctx A2=A; sae_process_commit(&C,A.scalar,A.elem+1); u8 cc[32]; sae_confirm(&C,1,cc); ck(sae_check_confirm(&A2,1,cc)!=0,"SAE wrong password rejected");
    { u8 lr[32],lm[32],lc[98],pc[98],ek[32],ep[32],ei[16]; hexb("992465fd3daa3c60aa6565b7f62a2a7f2e12dd12f198faf4fbed89d7ff1ace94",lr); hexb("9507a90f777a044d6a0830b91ea3d5dd70bece44e1acffb86983b5e1bf9fb322",lm); hexb("13002e2c0f0db52440ad146d967114ce005ce1eab0aa2c2e5c2871b774f6c2575c65d5ad9e00829707aa36ba8b859738fc961d08243505f47c035376d7ac4bc8d7b95083bf43827d0fc31ed778dd3671fd21a46d1091d64b6f9a1e1272621325dbe1",lc); hexb("1300591b96f3397fb945100848e7b550543b6720d88337ee93fc49fd6df7e08b5223e71b9bb048d3873f20556953a96c91536fd8ee6ca9b4a68a148b056a909be03e83ae208f60f8ef5537858074db06687032399862999b511e0a1552a5fea317c2",pc); hexb("1e733f6d9bd53256287304338831b09a39406d121017073a5c30db36f36cb81a",ek); hexb("4e4dfab1a2dd8ac1a91790f953faaa452ae5c6873ab75b63605ba663f8a7fe59",ep); hexb("8747a600eea3f9f22475df58ca1e5498",ei);
      sae_ctx V; memset(&V,0,sizeof V); ck(sae_derive_pwe(a1,a2,"mekmitasdigoat",V.pwe)==0,"SAE PWE (IEEE 802.11 Annex J.10)");
      memcpy(V.rnd,lr,32); p256_modn_add(lr,lm,V.scalar); p256_mul(lm,V.pwe,V.elem); p256_neg(V.elem);
      ck(!memcmp(V.scalar,lc+2,32),"SAE commit scalar matches Annex J.10"); ck(!memcmp(V.elem+1,lc+34,64),"SAE commit element matches Annex J.10");
      ck(sae_process_commit(&V,pc+2,pc+34)==0,"SAE process peer commit (Annex J.10)");
      ck(!memcmp(V.kck,ek,32),"SAE KCK matches Annex J.10"); ck(!memcmp(V.pmk,ep,32),"SAE PMK matches Annex J.10"); ck(!memcmp(V.pmkid,ei,16),"SAE PMKID matches Annex J.10"); }
    { u8 pwe[65]; sae_derive_pwe(a1,a2,"mekmitasdigoat",pwe); printf("PWE.x "); for(int i=0;i<32;i++)printf("%02x",pwe[1+i]); printf("\n"); }
    u8 kk[32]; memset(kk,0x0b,32); u8 out[48]; kdf_sha256(kk,32,"Pairwise key expansion",m,16,out,384); ck(out[0]||out[1],"KDF-SHA256 runs");
    { u8 h[16],e2[16]; md4_hash((const u8*)"abc",3,h); hexb("a448017aaf21d8525fc10ae87aa6729d",e2); ck(!memcmp(h,e2,16),"MD4 abc (RFC 1320)");
      md4_hash((const u8*)"12345678901234567890123456789012345678901234567890123456789012345678901234567890",80,h); hexb("e33b4ddc9c38f2199c3e7b164fcc0536",e2); ck(!memcmp(h,e2,16),"MD4 80 digits"); }
    { u8 k8[8],pt[8],ct[8],ex[8]; hexb("133457799BBCDFF1",k8); hexb("0123456789ABCDEF",pt); hexb("85E813540F0AB405",ex); des_encrypt(k8,pt,ct); ck(!memcmp(ct,ex,8),"DES known answer"); }
    { u8 ac[16],pc[16],ch[8],ex[24],nt[24],ph[16]; hexb("5B5D7C7D7B3F2F3E3C2C602132262628",ac); hexb("21402324255E262A28295F2B3A337C7E",pc);
      mschap_challenge_hash(pc,ac,"User",ch); hexb("D02E4386BCE91226",ex); ck(!memcmp(ch,ex,8),"MSCHAPv2 ChallengeHash (RFC 2759)");
      mschap_pw_hash("clientPass",ph); hexb("44EBBA8D5312B8D611474411F56989AE",ex); ck(!memcmp(ph,ex,16),"MSCHAPv2 PasswordHash (RFC 2759)");
      mschap_nt_response(ac,pc,"User","clientPass",nt); hexb("82309ECD8D708B5EA08FAA3981CD83544233114A3D85D6DF",ex); ck(!memcmp(nt,ex,24),"MSCHAPv2 NT-Response (RFC 2759)");
      char ar[43]; mschap_auth_response("clientPass",nt,pc,ac,"User",ar); ck(!strcmp(ar,"S=407A5589115FD0D6209F510FE9C04566932CDA56"),"MSCHAPv2 AuthenticatorResponse (RFC 2759)"); }
    printf("%d passed, %d failed\n",passes,fails); return fails?1:0; }
