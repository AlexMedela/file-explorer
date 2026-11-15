// cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function tools/text-caret-selftest.c -o /tmp/fe-caret-test -lm
#include "../src/base.h"
#include <assert.h>
#include "../src/text_caret.c"
int main(void) {
    TextCaret caret={0}; float alpha;
    u64 t=1000000000ull;
    assert(text_caret_animate(&caret,10,1,1,t,false,&alpha)==10 && alpha==1);
    float x=text_caret_animate(&caret,30,2,2,t+10000000,false,&alpha);
    assert(x==10 && alpha==1);
    x=text_caret_animate(&caret,30,2,2,t+55000000,false,&alpha);
    assert(x>10 && x<30);
    assert(text_caret_animate(&caret,30,2,2,t+110000000,false,&alpha)==30);
    for(int ms=120;ms<2000;ms+=30) {
        text_caret_animate(&caret,30,2,2,t+(u64)ms*1000000,false,&alpha);
        assert(alpha>=0.049f && alpha<=1.001f);
    }
    caret.input=t+2000000000ull;
    text_caret_animate(&caret,30,2,2,caret.input,false,&alpha); assert(alpha==1);
    assert(text_caret_animate(&caret,60,5,5,caret.input+1,true,&alpha)==60 && alpha==1);
    puts("caret animation: passed");
}
