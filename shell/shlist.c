int sh_list_cut(char* s,int* op){
    int sq=0,dq=0;
    for(int i=0;s[i];i++){
        char c=s[i];
        if(c=='\\'&&s[i+1]){ i++; continue; }
        if(c=='\''&&!dq){ sq=!sq; continue; }
        if(c=='"'&&!sq){ dq=!dq; continue; }
        if(sq||dq)continue;
        if(c==';'){ *op=';'; s[i]=0; return i+1; }
        if(c=='&'&&s[i+1]=='&'){ *op='&'; s[i]=0; return i+2; }
        if(c=='|'&&s[i+1]=='|'){ *op='|'; s[i]=0; return i+2; }
    }
    *op=0; return -1;
}
int sh_list_skip(int prev,int rc){ return (prev=='&'&&rc!=0)||(prev=='|'&&rc==0); }
