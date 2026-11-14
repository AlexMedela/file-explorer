// Preserve icon hue and internal shading while improving pale artwork on light
// navigation surfaces. Raw icon pixels and caches remain theme-independent.
static f32 icon_pixel_lightness(const u8 *pixels, size_t count) {
    double light = 0, weight = 0;
    for (size_t i = 0; i < count; i++) {
        const u8 *p = pixels + i * 4;
        light += (0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]) * p[3];
        weight += p[3];
    }
    return weight ? (f32)(light / (255.0 * weight)) : 0;
}
static f32 icon_contrast_scale(f32 ink, f32 background) {
    if (background < 0.65f || ink <= 0) return 1.f;
    return MIN(1.f, MAX(0.15f, background - 0.45f) / ink);
}
