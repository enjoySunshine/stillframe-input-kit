/*******************************************************************************************
*
*   test_wallclock.c - Prove real wall-clock pauses never enter the hold duration
*
*   This test actually sleeps (real wall-clock time) between building/re-reading frames and
*   verifies the frame-derived hold values are unaffected. It is the direct answer to:
*     "按住时长不能把停留观看的墙钟算进去" and "点按钮之前犹豫的那两秒不能加上".
*
*   LICENSE: zlib/libpng
*
********************************************************************************************/

#define _DEFAULT_SOURCE         // Required for usleep() under -std=c99

#include "framereplay.h"

#include <stdio.h>
#include <unistd.h>             // Required for: usleep()

static int failures = 0;
#define CHECK(cond, name) do { if (cond) printf("  [PASS] %s\n", name); else { failures++; printf("  [FAIL] %s\n", name); } } while(0)

int main(void)
{
    InitWindow(320, 240, "wallclock");
    printf("== wall-clock isolation tests (real sleeps inserted) ==\n");

    const double step = 100.0;
    FrameTimeline tl = LoadFrameTimeline(8, step);
    const FrameSample *prev = NULL;

    // Frame 11: press at (80,60). Build it, then waste real time before building frame 12.
    Vector2 p11[1] = { { 80.0f, 60.0f } };
    FrameSample s11 = FrBuildSample(11, 11*step, true, 69, true, p11, 1, prev, step);
    FrAppendSample(&tl, s11);
    prev = &tl.frames[tl.count - 1];

    printf("\nsleeping 0.3s of wall clock while parked on frame 11 ...\n");
    usleep(300000);   // Real delay: emulates the reviewer staring at the frozen frame

    // Re-reading frame 11 repeatedly must still report zero hold regardless of the sleep.
    const FrameSample *r = FrGetFrame(&tl, 11);
    CHECK(r->holdMs == 0.0, "frame 11 hold stays 0ms after a 300ms real sleep");

    // Frame 12: only +100ms in frame-timeline terms, even though much more wall time passed.
    Vector2 p12[1] = { { 140.0f, 60.0f } };
    FrameSample s12 = FrBuildSample(12, 12*step, true, 69, true, p12, 1, prev, step);
    FrAppendSample(&tl, s12);
    prev = &tl.frames[tl.count - 1];
    CHECK(FrGetFrame(&tl, 12)->holdMs == 100.0, "frame 12 hold is 100ms, not 100ms+sleep");

    printf("sleeping another 0.3s before building frame 13 ...\n");
    usleep(300000);   // Emulates the 2-second hesitation before pressing the step button

    Vector2 p13[1] = { { 140.0f, 60.0f } };
    FrameSample s13 = FrBuildSample(13, 13*step, true, 69, true, p13, 1, prev, step);
    FrAppendSample(&tl, s13);
    CHECK(FrGetFrame(&tl, 13)->holdMs == 200.0, "frame 13 hold adds only its own 100ms slice");

    // A non-uniform timeline: even if a frame's timestamp gap differs, hold follows data.
    // Build frame 14 with a 250ms timeline gap; hold must reflect exactly that, not wall time.
    Vector2 p14[1] = { { 200.0f, 60.0f } };
    FrameSample s14 = FrBuildSample(14, 13*step + 250.0, true, 69, true, p14, 1, prev, step);
    FrAppendSample(&tl, s14);
    CHECK(FrGetFrame(&tl, 14)->holdMs == 450.0, "non-uniform frame gap (250ms) honored from timestamp only");

    UnloadFrameTimeline(&tl);
    CloseWindow();
    printf("\n== wallclock: %d failures ==\n", failures);
    return failures? 1 : 0;
}
