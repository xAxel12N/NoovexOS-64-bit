#ifndef NWE_CSS_H
#define NWE_CSS_H

enum { SK_TAG, SK_UNIV, SK_ID, SK_CLASS, SK_ATTR, SK_PSEUDO };
enum { PS_HOVER = 1, PS_FOCUS, PS_ACTIVE, PS_FIRST, PS_LAST, PS_ONLY, PS_NTH, PS_NTHLAST, PS_NTHTYPE, PS_NTHLASTTYPE, PS_FIRSTTYPE, PS_LASTTYPE, PS_ONLYTYPE,
       PS_EMPTY, PS_ROOT, PS_LINK, PS_CHECKED, PS_DISABLED, PS_ENABLED, PS_NOT, PS_IS, PS_HAS, PS_FOCUSWITHIN, PS_PLACEHOLDER, PS_REQUIRED, PS_OPTIONAL,
       PS_TRUE, PS_FALSE, PS_OPEN, PS_HOST, PS_SLOTTED };
enum { AO_EXISTS = 1, AO_EQ, AO_WORD, AO_DASH, AO_PREFIX, AO_SUFFIX, AO_SUB };

typedef struct csel csel;
typedef struct { unsigned char kind, op, ci; char* name; char* val; int tag; csel** sub; int nsub; int a, b; } ssel;
typedef struct { ssel* s; int ns; char comb; } compound;
struct csel { compound* c; int nc; int spec; int pseudo; unsigned short ah[4]; unsigned char nah, ahdone, shk; };

typedef struct { short prop; unsigned char imp; char* name; char* val; } decl;
struct rule { csel** sel; int nsel; decl* d; int nd; int order; const char* media; int origin; int mgen; int mok; const char* scope; };
typedef struct { rule* r; int spec; } ment;

#define PROPS(X) \
 X(DISPLAY,"display") X(POSITION,"position") X(FLOAT,"float") X(CLEAR,"clear") X(BOXSIZING,"box-sizing") X(VISIBILITY,"visibility") \
 X(OVERFLOW,"overflow") X(OVERFLOWX,"overflow-x") X(OVERFLOWY,"overflow-y") X(WHITESPACE,"white-space") X(TEXTALIGN,"text-align") \
 X(TEXTDECORATION,"text-decoration") X(TEXTDECORATIONLINE,"text-decoration-line") X(TEXTTRANSFORM,"text-transform") X(FONTSTYLE,"font-style") \
 X(FONTFAMILY,"font-family") X(FONTWEIGHT,"font-weight") X(FONTSIZE,"font-size") X(LINEHEIGHT,"line-height") X(LETTERSPACING,"letter-spacing") \
 X(WORDSPACING,"word-spacing") X(TEXTINDENT,"text-indent") X(LISTSTYLE,"list-style") X(LISTSTYLETYPE,"list-style-type") X(LISTSTYLEPOSITION,"list-style-position") \
 X(VERTICALALIGN,"vertical-align") X(CURSOR,"cursor") X(BORDERCOLLAPSE,"border-collapse") X(WORDBREAK,"word-break") X(OVERFLOWWRAP,"overflow-wrap") X(WORDWRAP,"word-wrap") \
 X(POINTEREVENTS,"pointer-events") X(TEXTOVERFLOW,"text-overflow") X(OBJECTFIT,"object-fit") X(BACKGROUND,"background") X(BACKGROUNDCOLOR,"background-color") \
 X(BACKGROUNDIMAGE,"background-image") X(BACKGROUNDREPEAT,"background-repeat") X(BACKGROUNDSIZE,"background-size") X(BACKGROUNDPOSITION,"background-position") X(BACKGROUNDATTACHMENT,"background-attachment") \
 X(COLOR,"color") X(BORDER,"border") X(BORDERTOP,"border-top") X(BORDERRIGHT,"border-right") X(BORDERBOTTOM,"border-bottom") X(BORDERLEFT,"border-left") \
 X(BORDERWIDTH,"border-width") X(BORDERSTYLE,"border-style") X(BORDERCOLOR,"border-color") \
 X(BTW,"border-top-width") X(BRW,"border-right-width") X(BBW,"border-bottom-width") X(BLW,"border-left-width") \
 X(BTS,"border-top-style") X(BRS,"border-right-style") X(BBS,"border-bottom-style") X(BLS,"border-left-style") \
 X(BTC,"border-top-color") X(BRC,"border-right-color") X(BBC,"border-bottom-color") X(BLC,"border-left-color") \
 X(BORDERRADIUS,"border-radius") X(BTLR,"border-top-left-radius") X(BTRR,"border-top-right-radius") X(BBRR,"border-bottom-right-radius") X(BBLR,"border-bottom-left-radius") \
 X(OUTLINE,"outline") X(OUTLINEWIDTH,"outline-width") X(OUTLINESTYLE,"outline-style") X(OUTLINECOLOR,"outline-color") X(OUTLINEOFFSET,"outline-offset") \
 X(WIDTH,"width") X(HEIGHT,"height") X(MINWIDTH,"min-width") X(MINHEIGHT,"min-height") X(MAXWIDTH,"max-width") X(MAXHEIGHT,"max-height") \
 X(MARGIN,"margin") X(MT,"margin-top") X(MR,"margin-right") X(MB,"margin-bottom") X(ML,"margin-left") \
 X(PADDING,"padding") X(PT,"padding-top") X(PR,"padding-right") X(PB,"padding-bottom") X(PL,"padding-left") \
 X(TOP,"top") X(RIGHT,"right") X(BOTTOM,"bottom") X(LEFT,"left") X(INSET,"inset") X(ZINDEX,"z-index") X(OPACITY,"opacity") \
 X(FLEX,"flex") X(FLEXDIRECTION,"flex-direction") X(FLEXWRAP,"flex-wrap") X(FLEXFLOW,"flex-flow") X(FLEXGROW,"flex-grow") X(FLEXSHRINK,"flex-shrink") X(FLEXBASIS,"flex-basis") \
 X(JUSTIFYCONTENT,"justify-content") X(ALIGNITEMS,"align-items") X(ALIGNSELF,"align-self") X(ALIGNCONTENT,"align-content") X(JUSTIFYITEMS,"justify-items") X(JUSTIFYSELF,"justify-self") \
 X(PLACEITEMS,"place-items") X(PLACECONTENT,"place-content") X(PLACESELF,"place-self") X(ORDER,"order") X(GAP,"gap") X(ROWGAP,"row-gap") X(COLUMNGAP,"column-gap") \
 X(GRIDGAP,"grid-gap") X(GRIDROWGAP,"grid-row-gap") X(GRIDCOLUMNGAP,"grid-column-gap") X(GTC,"grid-template-columns") X(GTR,"grid-template-rows") X(GRIDTEMPLATE,"grid-template") \
 X(GRIDAUTOFLOW,"grid-auto-flow") X(GRIDAUTOROWS,"grid-auto-rows") X(GRIDAUTOCOLUMNS,"grid-auto-columns") X(GRIDCOLUMN,"grid-column") X(GRIDROW,"grid-row") \
 X(GCS,"grid-column-start") X(GCE,"grid-column-end") X(GRS,"grid-row-start") X(GRE,"grid-row-end") X(GRIDAREA,"grid-area") X(GTA,"grid-template-areas") X(GRID,"grid") \
 X(BOXSHADOW,"box-shadow") X(TEXTSHADOW,"text-shadow") X(TRANSFORM,"transform") X(CONTENT,"content") X(BORDERSPACING,"border-spacing") X(FONT,"font") \
 X(CARETCOLOR,"caret-color") X(ACCENTCOLOR,"accent-color") X(TRANSLATE,"translate") X(SCALE,"scale") X(ROTATE,"rotate") X(ASPECTRATIO,"aspect-ratio") \
 X(MARGININLINE,"margin-inline") X(MARGINBLOCK,"margin-block") X(PADDINGINLINE,"padding-inline") X(PADDINGBLOCK,"padding-block") \
 X(MIS,"margin-inline-start") X(MIE,"margin-inline-end") X(MBS,"margin-block-start") X(MBE,"margin-block-end") \
 X(PIS,"padding-inline-start") X(PIE,"padding-inline-end") X(PBS,"padding-block-start") X(PBE,"padding-block-end") \
 X(INLINESIZE,"inline-size") X(BLOCKSIZE,"block-size") X(MININLINESIZE,"min-inline-size") X(MAXINLINESIZE,"max-inline-size") \
 X(INSETINLINE,"inset-inline") X(INSETBLOCK,"inset-block") X(INSETINLINESTART,"inset-inline-start") X(INSETINLINEEND,"inset-inline-end") \
 X(TEXTALIGNLAST,"text-align-last") X(FILL,"fill") X(STROKE,"stroke") X(WEBKITLINECLAMP,"-webkit-line-clamp") X(USERSELECT,"user-select") X(APPEARANCE,"appearance") X(CLIP,"clip") X(CLIPPATH,"clip-path") X(BGCLIP,"background-clip") X(TEXTFILL,"text-fill-color") \
 X(SRC,"src") X(UNICODERANGE,"unicode-range") X(TRANSFORMORIGIN,"transform-origin") X(ANIMATION,"animation") X(ANIMNAME,"animation-name") X(ANIMDUR,"animation-duration") X(ANIMDELAY,"animation-delay") X(ANIMITER,"animation-iteration-count") X(ANIMDIR,"animation-direction") X(ANIMFILL,"animation-fill-mode") X(ANIMTIMING,"animation-timing-function") X(ANIMPLAY,"animation-play-state") \
 X(TRANSITION,"transition") X(TRANSPROP,"transition-property") X(TRANSDUR,"transition-duration") X(TRANSDELAY,"transition-delay") X(TRANSTIMING,"transition-timing-function") \
 X(BORDERINLINE,"border-inline") X(BORDERBLOCK,"border-block") X(BORDERBLOCKEND,"border-block-end") X(BORDERBLOCKSTART,"border-block-start") X(BORDERINLINESTART,"border-inline-start") X(BORDERINLINEEND,"border-inline-end")

#define PX(a, b) P_##a,
enum { PROPS(PX) P_NPROPS, P_CUSTOM = 1000 };
#undef PX

int css_prop_id(const char* name);
int css_parse_decls(const char* s, const char* e, decl** out);
int css_gather(node* n, ment* out, int cap, int pseudo);
int css_match_sel(csel* cs, node* n);
void css_rebuild_if_needed(void);
void css_anc_push(node* n);
void css_anc_pop(node* n);
void css_anc_reset(void);
extern int g_css_mgen;
extern const char* css_prop_names[];

#endif
