/*******************************************************************************************
*
*   framereplay.h - Deterministic, frame-indexed input/gesture replay with dual backends
*
*   WHY THIS MODULE EXISTS
*   ----------------------
*   The stock raylib gesture/automation path has three properties that break frame-accurate
*   review work:
*
*     1. Hold duration is derived from wall-clock time (rgestures.h uses GetTime()).
*        Freezing on one frame and staring at it keeps adding seconds to the hold readout.
*     2. Mouse/keyboard state injected through PlayAutomationEvent() never reaches the gesture
*        layer: on desktop the mouse->touch translation lives inside GLFW hardware callbacks,
*        and the memory (windowless) backend has no input callbacks at all. So a recorded press
*        either never shows up, or only shows on the first pass.
*     3. Replay advances by "how many times the loop ran", not by frame index. Re-sampling a
*        frozen frame re-fires edge events (a brand new press), re-accumulates pointer deltas
*        and makes the frame counter drift over a long run.
*
*   This module replaces that with a pure, frame-indexed model:
*
*     * The whole take is stored once, one FrameSample per frame, addressed by frame number.
*     * Every quantity that can be displayed is derived from the frame's own timestamp, never
*       from the wall clock. Re-reading the same frame returns byte-for-byte the same answer.
*     * Edges (press/release) and per-frame deltas are computed from adjacent frames at record
*       time and stored. Seeking back to a frame does not re-run any state machine.
*     * A single render routine writes into a backend-agnostic canvas. Two thin backends bind
*       it to a live window (Draw*) or to a windowless RAM Image (ImageDraw*), so the dot and
*       the label land on identical coordinates in both outputs.
*
*   The module does not touch gamepad rumble, audio, fonts beyond the default font, models or
*   gameplay. Click, drag and wheel handling in normal (non-frozen) playback is preserved.
*
*   LICENSE: zlib/libpng
*
********************************************************************************************/

#ifndef FRAMEREPLAY_H
#define FRAMEREPLAY_H

#include "raylib.h"

#if defined(__cplusplus)
extern "C" {            // Prevents name mangling of functions
#endif

//----------------------------------------------------------------------------------
// Defines
//----------------------------------------------------------------------------------
#define FR_MAX_POINTS            2       // Fingers tracked per frame (1 = mouse, 2 = pinch)
#define FR_MAX_TEXT              40      // Held label text length

//----------------------------------------------------------------------------------
// Types and Structures Definition
//----------------------------------------------------------------------------------

// One finger / touch point as recorded for a single frame.
// position is in screen pixels; dx/dy are the movement delta relative to the previous frame
// (already computed at record time, so re-reading this frame never recomputes them).
typedef struct FramePoint {
    bool active;                    // Is this finger down on this frame?
    Vector2 position;               // Pixel position on this frame
    Vector2 delta;                  // Position minus previous frame position (0,0 when stationary)
} FramePoint;

// A single immutable frame of the take. Everything needed to render and reason about the
// frame is here; nothing is fetched from live input or the wall clock.
typedef struct FrameSample {
    int frame;                      // Frame number (0-based), also the timeline key
    double timeMs;                  // Frame timestamp in milliseconds (relative to frame 0)

    bool keyDown;                   // Keyboard key held on this frame
    int key;                        // Keyboard key code (0 when irrelevant)
    bool keyPressed;                // True only on the single frame the key went down

    bool buttonDown;                // Mouse/touch button held on this frame
    bool buttonPressed;             // True only on the single frame the button went down
    bool buttonReleased;            // True only on the single frame the button came up

    FramePoint points[FR_MAX_POINTS];   // Finger 0 (mouse) and finger 1 (pinch partner)

    int gesture;                    // Gesture id for this frame (raylib GESTURE_* values)
    double holdMs;                  // Hold duration in ms derived from frame timestamps
    Vector2 dragVector;             // Drag vector in pixels for this frame
    float dragAngle;                // Drag angle in degrees
    Vector2 pinchVector;            // Pinch vector (point B - point A) in pixels
    float pinchAngle;               // Pinch angle in degrees
} FrameSample;

// The recorded take: a dense array of frames plus the fixed frame step used by the timeline.
typedef struct FrameTimeline {
    FrameSample *frames;            // Dense frame array, index == frame number
    int count;                      // Number of frames recorded
    int capacity;                   // Allocated capacity
    double frameStepMs;             // Nominal time between frames (e.g. 100.0)
    int frameStepKey;               // Keyboard key used at record time (for label), 0 = none
} FrameTimeline;

// Backend-agnostic drawing surface. A window backend routes every primitive to the GPU
// Draw* calls; an image backend routes the exact same primitives to ImageDraw* on a RAM image.
typedef struct FrCanvas {
    Image *target;                  // NULL => window backend (uses Draw*); set => image backend
} FrCanvas;

//----------------------------------------------------------------------------------
// Timeline lifecycle
//----------------------------------------------------------------------------------
FrameTimeline LoadFrameTimeline(int capacity, double frameStepMs);
void UnloadFrameTimeline(FrameTimeline *timeline);

// Append a frame built from live-ish inputs. prev is the prior frame (may be NULL on frame 0);
// the routine derives edges, deltas and the frame-derived hold time deterministically.
// timeMs is the frame's own timestamp; pass frame*frameStepMs for an even timeline.
FrameSample FrBuildSample(int frame, double timeMs,
                          bool buttonDown, int key, bool keyDown,
                          const Vector2 *positions, int pointCount,
                          const FrameSample *prev, double frameStepMs);
void FrAppendSample(FrameTimeline *timeline, FrameSample sample);

// Direct, deterministic frame access. No state machine runs; the same frame always returns
// the same pointer/values no matter how many times it is requested.
const FrameSample *FrGetFrame(const FrameTimeline *timeline, int frame);
int FrClampFrame(const FrameTimeline *timeline, int frame);

//----------------------------------------------------------------------------------
// Deterministic gesture classification (pure functions over adjacent frames)
//----------------------------------------------------------------------------------
// Classify the gesture for a frame from its own and the previous sample. Pure: no globals,
// no GetTime(), no live input. Repeated calls on identical inputs return identical output.
int FrClassifyGesture(const FrameSample *cur, const FrameSample *prev, double frameStepMs);

// Frame-derived hold duration in milliseconds. Counts only whole frames actually spent held,
// so a frozen frame's value never grows with viewing time and a 2-second pause between
// manual steps is never added.
double FrComputeHoldMs(const FrameSample *cur, const FrameSample *timelineBase, double frameStepMs);

//----------------------------------------------------------------------------------
// Backend-agnostic rendering (identical layout on window and RAM image)
//----------------------------------------------------------------------------------
void FrCanvasClear(FrCanvas canvas, Color color);
void FrCanvasCircle(FrCanvas canvas, Vector2 center, float radius, Color color);
void FrCanvasLine(FrCanvas canvas, Vector2 a, Vector2 b, float thickness, Color color);
void FrCanvasText(FrCanvas canvas, const char *text, int x, int y, int fontSize, Color color);

// Render one frozen/playing frame into the canvas. The dot is drawn at the recorded pixel
// position; the label shows the gesture name and the frame-derived hold in milliseconds.
void FrRenderFrame(FrCanvas canvas, const FrameSample *sample, int screenWidth, int screenHeight);

// Human readable gesture name (mirrors rgestures ids).
const char *FrGestureName(int gesture);

// Build a windowless RAM image of a frame at the given size. Caller owns the image
// (UnloadImage when done). This is the "memory picture" that needs no window/GPU.
Image FrRenderFrameToImage(const FrameTimeline *timeline, int frame, int width, int height);

#if defined(__cplusplus)
}
#endif

#endif // FRAMEREPLAY_H
