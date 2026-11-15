# B2 report (bug hunter, daily-use desktop paths) - v11.04

Everything below is read-only analysis. Line numbers are from v11.03. Ordered by how much data the owner could lose.

## 1. HIGH: opening a file from the USB stick in Files silently overwrites a local file with the same name
apps/system/file_explorer/clicks_and_drops.c:207 calls `fs_save_bin(ufs[k].name, buf, n, 1)`. fs_save_bin (sys/native/syscall.c:212) is "create or overwrite" and matches the name anywhere on the disk, not just in Downloads. fx_fname_used is a stub that always returns 0 (trash_and_sort.c:174), so duplicate names in different folders are allowed and it is easy to have one.
How the owner would notice: he edits notes.md or report.docx locally, then clicks a file with the same name on the stick. His local copy is replaced by the stick's version, and the toast still says "(copied to Downloads)". The same overwrite happens to an older copy in any folder.
Patch (clicks_and_drops.c:207):
```c
                char nm[64]; scopy(nm,ufs[k].name)[0]=0;
                for(int t=2;t<100;t++){ int hit=0; for(int j=0;j<FS_N;j++) if(FS[j].used&&seq(FS[j].name,nm)){ hit=1; break; } if(!hit)break;
                    char* p=nm; const char* s=ufs[k].name; int i=0; for(;s[i]&&s[i]!='.'&&i<40;i++)*p++=s[i]; p=scopy(p," ("); p=udec(p,(u32)t); *p++=')'; for(const char* e=s+i;*e&&p<nm+62;)*p++=*e++; *p=0; }
                int s=fs_save_bin(nm,(const u8*)buf,n,1); kfree(buf);
```
(Better: reuse fx_unique(fx_downloads_dir, ...), but it is static in trash_and_sort.c, which is a sibling TU, so check whether the folder is unity-included first.)

## 2. HIGH: Writer "New" then Ctrl+S overwrites yesterday's Document1.docx without asking
apps/office/writer.c:236 (`cmd(1)`) resets the name to "Document1.docx" and sets wp_slot=-1. Then do_save (:233) -> save_docx (:180) -> fs_save_bin overwrites any existing Document1.docx anywhere on the disk. "Save as" (:285) has the same silent overwrite for any typed name.
How the owner would notice: he writes a document, saves it, reboots, clicks New, types, presses Ctrl+S. The first document is gone.
Patch (writer.c:233, do_save):
```c
static void do_save(void){ if(wp_slot<0){ for(int j=0;j<FS_N;j++) if(FS[j].used&&seq(FS[j].name,wp_name)){ cmd(4); toast("A file with that name exists - pick another name"); return; } }
    int r=save_docx(wp_name); char t[96],*q=t; q=scopy(q,r>=0?"Saved ":"Could not save "); q=scopy(q,wp_name); *q=0; toast(t); }
```
In the Save-as Enter branch (:285), set `wp_slot=-1;` before do_save() so the check also runs there (the user then has to type a new name). If he really wants to overwrite he types the same name again; an "Overwrite?" yes/no modal is a nice extra.

## 3. HIGH: Editor "Save" does nothing for new files or files opened from USB, and stays silent
apps/office/editor.c:76: `if(ed_file<0) return;`. ed_file is -1 after New (:189), after editor_open_buf, and after ed_load_usb (:72). There is no toast, ed_dirty stays 1, and the window says "Not saved yet"/"Modified". Closing the editor then loses everything.
Patch (editor.c:76, replace `if(ed_file<0) return;`):
```c
    if(ed_file<0){ static char b[ED_ROWS*(ED_COLS+1)]; int n=ed_text(b,(int)sizeof b); char nm[24]; scopy(nm,"Untitled.txt")[0]=0;
        for(int t=2;t<100;t++){ int hit=0; for(int j=0;j<FS_N;j++) if(FS[j].used&&seq(FS[j].name,nm)){ hit=1; break; } if(!hit)break; char* p=scopy(nm,"Untitled "); p=udec(p,(u32)t); scopy(p,".txt")[0]=0; }
        int s=fs_save_bin(nm,(const u8*)b,n,0); if(s<0){ toast("Could not save"); return; } ed_file=s; ed_dirty=0; char m[48]; scopy(scopy(m,"Saved as "),nm)[0]=0; toast(m); return; }
```
(ed_text is already used by py_save at :36. Check that its prototype is visible above editor_save.)

## 4. MEDIUM-HIGH: Editor truncates big files on open, then Save writes the truncated text back
editor.c:56-58 keeps at most ED_ROWS-1=199 lines and ED_COLS-1=199 characters per line, and stops at the first NUL byte. editor_save (:77-78) rewrites the whole file from ED. This extends the known "editor truncating long lines" issue to row count and NUL bytes: a 300-line log or config loses lines 200 and up on the first Save.
Minimal safe patch: add `static int ed_trunc;`. In editor_open, set `ed_trunc=(k<dl)||(any line was cut)`: put `else ed_trunc=1;` on the `c<ED_COLS-1` branch, and after the loop add `if(k<dl)ed_trunc=1;` (hoist k out of the for). In editor_reset, set `ed_trunc=0;`. In editor_save, after the permission check:
```c
    if(ed_trunc){ toast("File is larger than the editor can show - not saved, to protect the rest of the file"); return; }
```

## 5. MEDIUM: Writer New and Open, and Editor New, throw away unsaved changes with no prompt
writer.c:236 (New, also Ctrl+N at :291) and :237/writer_open_slot (Open, Ctrl+O) replace the document even when wp_mod=1. editor.c:189 (New) does the same with ed_dirty=1. One mis-click on "New" next to "Open"/"Save" wipes the work. Undo does not help: snap(3) is taken, but doc_reset clears ucnt? Check this, because writer open_docx sets ucnt=0.
Smallest patch, a "press twice" guard:
```c
static int wp_warned; /* writer.c */
if(id==1||id==2){ if(wp_mod&&!wp_warned){ wp_warned=1; toast("Unsaved changes - press again to discard, or Save first"); return; } wp_warned=0; }
```
Put it at the top of cmd(). Reset wp_warned=0 in do_save and on any typed key. Use the same pattern in editor_click id==1 with ed_dirty.

## 6. LOW: drag-and-drop move in Files skips the permission check that paste-move has
clicks_and_drops.c:31-33 (fx_drop) moves FS[slot] to target without `perm_dir(target)` / `perm_dir(FS[slot].dir)`. fx_paste (trash_and_sort.c:287) checks both. A normal user can drag a root-owned file into or out of a protected folder.
Patch at :31: `if(!perm_dir(target)||!perm_dir(FS[slot].dir)){ toast("Permission denied"); return 1; }`

Not reached in the 8 minutes: wm.c snap/minimize, alt-tab, Start search, taskmgr, Terminal.
