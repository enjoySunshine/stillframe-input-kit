/*******************************************************************************************
*
*   replay_viewer.c - Window viewer for the deterministic frame-indexed replay
*
*   This is the "open a window and watch the dot and milliseconds" half of the workflow.
*   It drives the exact same FrameTimeline and FrRenderFrame() as the headless image path;
*   the only difference is the FrCanvas routes primitives to Draw* (GPU window) instead of
*   ImageDraw* (RAM image).
*
*   Controls:
*     SPACE        toggle freeze / normal playback
*     RIGHT/LEFT   step one frame while frozen (a 2-second pause before pressing changes
*                  nothing: hold time only ever advances by the frame's own slice)
*     R            restart at frame 0
*     S            save the current frozen/playing frame to a windowless RAM image PNG,
*                  proving the memory picture matches the window frame at the same index
*
*   Build (desktop, requires GL/GLFW):
*     gcc replay_viewer.c framereplay.c scenario.c -lraylib -lm -o replay_viewer
*   The headless PLATFORM_MEMORY build is covered by test_framereplay / export_frames.
*
*   LICENSE: zlib/libpng
*
********************************************************************************************/

#include "raylib.h"
#include "framereplay.h"
#include "scenario.c"

#define VIEWER_WIDTH     TEST_SCREEN_WIDTH
#define VIEWER_HEIGHT    TEST_SCREEN_HEIGHT

int main(void)
{
    InitWindow(VIEWER_WIDTH, VIEWER_HEIGHT, "framereplay - frame accurate review");
    SetTargetFPS(60);

    FrameTimeline timeline = FrBuildScenario();

    bool frozen = true;                 // Start parked on the director's frame for review
    int currentFrame = 11;              // Begin on frame 11 (the press under review)
    currentFrame = FrClampFrame(&timeline, currentFrame);

    while (!WindowShouldClose())
    {
        //------------------------------------------------ Update ----------------
        if (IsKeyPressed(KEY_SPACE)) frozen = !frozen;
        if (IsKeyPressed(KEY_R)) { currentFrame = 0; frozen = true; }

        if (frozen)
        {
            // Manual stepping. Repeatedly reading one frame never mutates it; frame number
            // only changes when the reviewer explicitly steps.
            if (IsKeyPressed(KEY_RIGHT)) currentFrame = FrClampFrame(&timeline, currentFrame + 1);
            if (IsKeyPressed(KEY_LEFT))  currentFrame = FrClampFrame(&timeline, currentFrame - 1);
        }
        else
        {
            // Normal playback: advance one recorded frame per rendered frame, then loop.
            currentFrame++;
            if (currentFrame >= timeline.count) currentFrame = 0;
        }

        const FrameSample *sample = FrGetFrame(&timeline, currentFrame);

        if (IsKeyPressed(KEY_S))
        {
            // Produce the windowless memory picture of THIS frame and write it to disk.
            Image memoryPicture = FrRenderFrameToImage(&timeline, currentFrame, VIEWER_WIDTH, VIEWER_HEIGHT);
            ExportImage(memoryPicture, TextFormat("viewer_frame%02d_memory.png", currentFrame));
            UnloadImage(memoryPicture);
        }

        //-------------------------------------------------- Draw ----------------
        BeginDrawing();
        {
            FrCanvas windowCanvas = { 0 };   // target == NULL => window/GPU backend
            FrRenderFrame(windowCanvas, sample, VIEWER_WIDTH, VIEWER_HEIGHT);

            DrawText(TextFormat("%s  frame %i/%i  [SPACE] %s  [<-][->] step  [S] save image",
                                frozen? "FROZEN" : "PLAYING", currentFrame, timeline.count - 1,
                                frozen? "play" : "freeze"),
                     8, VIEWER_HEIGHT - 18, 12, DARKPURPLE);
        }
        EndDrawing();
    }

    UnloadFrameTimeline(&timeline);
    CloseWindow();
    return 0;
}
