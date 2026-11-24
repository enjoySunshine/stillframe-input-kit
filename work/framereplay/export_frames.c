/*******************************************************************************************
*
*   export_frames.c - Render every key frame to a windowless RAM image and export PNGs
*
*   Produces visual evidence (one PNG per frame) using only the memory/Image backend.
*   The same FrRenderFrame routine powers the window backend on a desktop build.
*
*   LICENSE: zlib/libpng
*
********************************************************************************************/

#include "framereplay.h"
#include "scenario.c"
#include <stdio.h>
#include <math.h>

static int DarkPixelsInRect(Image img, int x0, int y0, int x1, int y1)
{
    int n = 0;
    Color *c = (Color *)img.data;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            Color p = c[y*img.width + x];
            if ((p.r < 100) && (p.g < 100) && (p.b < 100)) n++;
        }
    return n;
}

// A red-ish (finger) pixel at a given recorded coordinate proves the dot landed there.
static bool RedishAt(Image img, int x, int y)
{
    Color p = ((Color *)img.data)[y*img.width + x];
    return (p.r > 180 && p.g < 130 && p.b < 130);
}

int main(void)
{
    InitWindow(TEST_SCREEN_WIDTH, TEST_SCREEN_HEIGHT, "export");
    FrameTimeline tl = FrBuildScenario();

    const int frames[] = { 10, 11, 12, 13, 14, 20, 21 };
    for (unsigned i = 0; i < sizeof(frames)/sizeof(frames[0]); i++)
    {
        int f = frames[i];
        const FrameSample *s = FrGetFrame(&tl, f);
        Image img = FrRenderFrameToImage(&tl, f, TEST_SCREEN_WIDTH, TEST_SCREEN_HEIGHT);
        char name[64];
        snprintf(name, sizeof(name), "frame%02d_memory.png", f);
        ExportImage(img, name);
        printf("exported %s  gesture=%-9s hold=%4.0fms pos=(%.0f,%.0f) d=(%.0f,%.0f)\n",
               name, FrGestureName(s->gesture), s->holdMs,
               s->points[0].position.x, s->points[0].position.y,
               s->points[0].delta.x, s->points[0].delta.y);
        UnloadImage(img);
    }

    // Coordinate-contract checks: the dot must sit exactly on the recorded pointer pixel.
    Image f11 = FrRenderFrameToImage(&tl, 11, TEST_SCREEN_WIDTH, TEST_SCREEN_HEIGHT);
    Image f12 = FrRenderFrameToImage(&tl, 12, TEST_SCREEN_WIDTH, TEST_SCREEN_HEIGHT);
    int ok = 1;
    ok &= RedishAt(f11, 80, 60);
    ok &= RedishAt(f12, 140, 60);
    // Frame 11 label band (gesture+hold) and frame 12 label band both contain dark text.
    ok &= (DarkPixelsInRect(f11, 90, 76, 300, 122) > 20);
    ok &= (DarkPixelsInRect(f12, 150, 76, 310, 122) > 20);
    printf("\ncoordinate contract (dot at recorded pos + label ink): %s\n", ok? "PASS":"FAIL");

    // Different-sized image of the SAME frame must still place the dot at the recorded pixel.
    Image f12big = FrRenderFrameToImage(&tl, 12, 480, 320);
    bool bigOk = RedishAt(f12big, 140, 60);
    printf("same frame at 480x320, dot still at (140,60): %s\n", bigOk? "PASS":"FAIL");
    ExportImage(f12big, "frame12_memory_480x320.png");

    UnloadImage(f11); UnloadImage(f12); UnloadImage(f12big);
    UnloadFrameTimeline(&tl);
    CloseWindow();
    return (ok && bigOk)? 0 : 1;
}
