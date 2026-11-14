// cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-function tools/icon-contrast-selftest.c -o /tmp/fe-icon-contrast-test -lm
#include "../src/base.h"
#include <assert.h>
#include "../src/icon_contrast.c"
int main(void) {
    u8 pale[] = {230,230,230,255, 255,255,255,0};
    f32 light = icon_pixel_lightness(pale,2);
    assert(fabsf(light-230.f/255.f)<0.001f); // transparent padding does not dilute the measurement
    f32 shade=icon_contrast_scale(light,0.96f);
    assert(shade<1 && light*shade<=0.511f);
    assert(icon_contrast_scale(0.25f,0.96f)==1); // already dark artwork stays unchanged
    assert(icon_contrast_scale(light,0.12f)==1); // dark surfaces keep original colors
    assert(icon_contrast_scale(0,0.96f)==1);
    assert(icon_pixel_lightness((u8[]){255,255,255,0},1)==0);
    puts("icon contrast: passed");
}
