/*******************************************************************************************
*
*   scenario.c - Builds the exact take described in the review brief and exposes it to tests
*
*   The take (frame numbers follow the brief; we keep them 0-based internally but label
*   everything with the same numbers the director uses):
*
*     frame 10: finger (80,60), button up, keyboard up
*     frame 11: finger (80,60), left button DOWN (press), key E DOWN (press), no movement
*     frame 12: finger (140,60), left button still down, +100ms, drag right by 60
*     frame 13: finger (140,60), still held, another +100ms (proves hold only grows per frame)
*     frame 14: button released at (140,60)
*     frame 20: two-finger pinch frame, then frame 21 re-sampled to prove it stays frozen
*
*   LICENSE: zlib/libpng
*
********************************************************************************************/

#include "framereplay.h"

#include <stddef.h>             // Required for: NULL

#ifndef TEST_SCREEN_WIDTH
#define TEST_SCREEN_WIDTH     320
#define TEST_SCREEN_HEIGHT    240
#endif
#define TEST_STEP_MS          100.0
#define TEST_KEY_E            69      // raylib KEY_E

// Build the deterministic take. Frames are appended in dense frame-number order.
FrameTimeline FrBuildScenario(void)
{
    FrameTimeline tl = LoadFrameTimeline(32, TEST_STEP_MS);
    tl.frameStepKey = TEST_KEY_E;

    const FrameSample *prev = NULL;

    // ---- Frames 0..9: idle, nothing pressed, pointer parked off-screen at (0,0) ----
    for (int f = 0; f < 10; f++)
    {
        Vector2 p[1] = { { 0.0f, 0.0f } };
        FrameSample s = FrBuildSample(f, f*TEST_STEP_MS, false, 0, false, p, 1, prev, TEST_STEP_MS);
        FrAppendSample(&tl, s);
        prev = &tl.frames[tl.count - 1];
    }

    // ---- Frame 10: finger at (80,60), not pressed, no key ----
    {
        Vector2 p[1] = { { 80.0f, 60.0f } };
        FrameSample s = FrBuildSample(10, 10*TEST_STEP_MS, false, 0, false, p, 1, prev, TEST_STEP_MS);
        FrAppendSample(&tl, s);
        prev = &tl.frames[tl.count - 1];
    }

    // ---- Frame 11: same (80,60), left button pressed AND key E pressed, zero movement ----
    {
        Vector2 p[1] = { { 80.0f, 60.0f } };
        FrameSample s = FrBuildSample(11, 11*TEST_STEP_MS, true, TEST_KEY_E, true, p, 1, prev, TEST_STEP_MS);
        FrAppendSample(&tl, s);
        prev = &tl.frames[tl.count - 1];
    }

    // ---- Frame 12: +100ms, finger (140,60), still held -> drag right 60 ----
    {
        Vector2 p[1] = { { 140.0f, 60.0f } };
        FrameSample s = FrBuildSample(12, 12*TEST_STEP_MS, true, TEST_KEY_E, true, p, 1, prev, TEST_STEP_MS);
        FrAppendSample(&tl, s);
        prev = &tl.frames[tl.count - 1];
    }

    // ---- Frame 13: still held at (140,60), another frame slice only ----
    {
        Vector2 p[1] = { { 140.0f, 60.0f } };
        FrameSample s = FrBuildSample(13, 13*TEST_STEP_MS, true, TEST_KEY_E, true, p, 1, prev, TEST_STEP_MS);
        FrAppendSample(&tl, s);
        prev = &tl.frames[tl.count - 1];
    }

    // ---- Frame 14: release at (140,60) ----
    {
        Vector2 p[1] = { { 140.0f, 60.0f } };
        FrameSample s = FrBuildSample(14, 14*TEST_STEP_MS, false, 0, false, p, 1, prev, TEST_STEP_MS);
        FrAppendSample(&tl, s);
        prev = &tl.frames[tl.count - 1];
    }

    // ---- Frames 15..19 idle ----
    for (int f = 15; f < 20; f++)
    {
        Vector2 p[1] = { { 0.0f, 0.0f } };
        FrameSample s = FrBuildSample(f, f*TEST_STEP_MS, false, 0, false, p, 1, prev, TEST_STEP_MS);
        FrAppendSample(&tl, s);
        prev = &tl.frames[tl.count - 1];
    }

    // ---- Frame 20: two-finger pinch (fingers separating) ----
    {
        Vector2 p[2] = { { 100.0f, 120.0f }, { 180.0f, 120.0f } };
        FrameSample s = FrBuildSample(20, 20*TEST_STEP_MS, true, 0, false, p, 2, prev, TEST_STEP_MS);
        FrAppendSample(&tl, s);
        prev = &tl.frames[tl.count - 1];
    }

    // ---- Frame 21: same two fingers, slightly further apart (pinch out continues) ----
    {
        Vector2 p[2] = { { 90.0f, 120.0f }, { 190.0f, 120.0f } };
        FrameSample s = FrBuildSample(21, 21*TEST_STEP_MS, true, 0, false, p, 2, prev, TEST_STEP_MS);
        FrAppendSample(&tl, s);
        prev = &tl.frames[tl.count - 1];
    }

    return tl;
}
