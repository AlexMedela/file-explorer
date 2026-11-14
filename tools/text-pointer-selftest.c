// cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function tools/text-pointer-selftest.c -o /tmp/fe-text-pointer-test -lm
#include "../src/base.h"
#include <assert.h>
typedef struct { f32 x,y,w,h; } Rect;
static bool in_rect(Rect r, f32 x, f32 y) { return x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h; }
#include "../src/text_caret.c"
typedef struct { char buf[PATH_MAX]; int caret,anchor; f32 scroll; TextCaret animation; } TextEdit;
typedef struct { f32 adv; } Glyph;
typedef struct { f32 width; } Font;
typedef struct { int button,clicks; f32 x,y; } SDL_MouseButtonEvent;
typedef struct { f32 x,y; } SDL_MouseMotionEvent;
enum { SDL_BUTTON_LEFT=1, SDL_KMOD_SHIFT=1 };
static int mods; static bool captured;
static int SDL_GetModState(void) { return mods; }
static void SDL_CaptureMouse(bool capture) { captured=capture; }
static struct { TextEdit edit,ren_te,search_te; void *edit_pane,*ren_pane,*search_pane; bool pal_open; } A;
static bool modal;
static bool sheet_open(void) { return modal; }
static int te_prev(const TextEdit *t,int i) { if(i>0) { i--; while(i>0 && ((u8)t->buf[i]&0xc0)==0x80) i--; } return i; }
static int te_next(const TextEdit *t,int i) { int n=(int)strlen(t->buf); if(i<n) { i++; while(i<n && ((u8)t->buf[i]&0xc0)==0x80) i++; } return i; }
static bool te_sep(char c) { return c==' ' || c=='/' || c=='.' || c=='-' || c=='_'; }
static bool text_complex(const char *s,int n) { (void)s; (void)n; return false; }
static int g_sh_n;
static int text_shape(Font *f,const char *s,int n,f32 *width) { (void)f;(void)s;(void)n;*width=0;return 0; }
static bool shaped_span(int n,int k,f32 *a,f32 *b) { (void)n;(void)k;*a=*b=0;return false; }
static Glyph *font_glyph(Font *f,u32 cp) { (void)cp; static Glyph glyph; glyph.adv=f->width;return &glyph; }
#include "../src/text_pointer.c"
static void click(f32 x,int count) { SDL_MouseButtonEvent b={1,count,x,10}; assert(te_pointer_down(&b)); }
int main(void) {
    Font font={10};
    A.ren_pane=&font; strcpy(A.ren_te.buf,"hello world.txt");
    te_register(&A.ren_te,&font,(Rect){0,0,100,20},5);
    click(25,1); assert(A.ren_te.caret==2 && A.ren_te.anchor==2 && captured);
    SDL_MouseMotionEvent motion={65,10}; assert(te_pointer_motion(&motion));
    assert(A.ren_te.anchor==2 && A.ren_te.caret==6); assert(te_pointer_up() && !captured);
    mods=SDL_KMOD_SHIFT; click(85,1); assert(A.ren_te.anchor==2 && A.ren_te.caret==8); te_pointer_up(); mods=0;
    click(75,2); assert(A.ren_te.anchor==6 && A.ren_te.caret==11 && !captured);
    click(25,3); assert(A.ren_te.anchor==0 && A.ren_te.caret==15);
    A.ren_te.scroll=50; click(15,1); assert(A.ren_te.caret==6); te_pointer_up();
    strcpy(A.ren_te.buf,"a\xC3\xA9" "b"); A.ren_te.scroll=0;
    click(25,1); assert(A.ren_te.caret==3); te_pointer_up();
    click(15,1); g_text_field_count=0;
    assert(!te_pointer_motion(&motion) && !captured);
    te_register(&A.ren_te,&font,(Rect){0,0,100,20},5); A.ren_pane=NULL;
    SDL_MouseButtonEvent b={1,1,25,10}; assert(!te_pointer_down(&b));
    A.ren_pane=&font; modal=true;
    assert(!te_pointer_down(&b)); // newly opened overlays block old field geometry
    puts("text pointer checks: passed");
}
