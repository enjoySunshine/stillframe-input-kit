/*******************************************************************************************
*
*   framereplay.c - Deterministic, frame-indexed input/gesture replay implementation
*
*   See framereplay.h for the design rationale. Every observable value in this file is a
*   function of frame data only: there is no GetTime(), no live input polling and no mutable
*   per-read state, which is what makes a frozen frame identical on every re-sample.
*
*   LICENSE: zlib/libpng
*
********************************************************************************************/

#include "framereplay.h"

#include <stdlib.h>             // Required for: calloc(), free()
#include <stdio.h>              // Required for: snprintf()
#include <math.h>               // Required for: sqrtf(), atan2f()

//----------------------------------------------------------------------------------
// Local helpers
//----------------------------------------------------------------------------------
static float FrVecDistance(Vector2 a, Vector2 b)
{
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    return sqrtf(dx*dx + dy*dy);
}

static float FrVecAngle(Vector2 from, Vector2 to)
{
    // Angle in degrees, raylib-style: 0 points right, measured counterclockwise, Y inverted
    float angle = atan2f(to.y - from.y, to.x - from.x)*(180.0f/PI);
    if (angle < 0.0f) angle += 360.0f;
    return angle;
}

// Mirror of the rgestures GESTURE_* ids so the module stays compatible with recorded data.
enum {
    FR_GESTURE_NONE        = 0,
    FR_GESTURE_TAP         = 1,
    FR_GESTURE_DOUBLETAP   = 2,
    FR_GESTURE_HOLD        = 4,
    FR_GESTURE_DRAG        = 8,
    FR_GESTURE_SWIPE_RIGHT = 16,
    FR_GESTURE_SWIPE_LEFT  = 32,
    FR_GESTURE_SWIPE_UP    = 64,
    FR_GESTURE_SWIPE_DOWN  = 128,
    FR_GESTURE_PINCH_IN    = 256,
    FR_GESTURE_PINCH_OUT   = 512
};

// Normalized thresholds (fraction of screen dimension), matching rgestures tuning so a
// recorded take classifies the same way the live system would have.
#define FR_MINIMUM_DRAG     0.015f
#define FR_MINIMUM_PINCH    0.005f
#define FR_FORCE_TO_SWIPE   0.2f
#define FR_TAP_TIMEOUT_MS   300.0

//----------------------------------------------------------------------------------
// Timeline lifecycle
//----------------------------------------------------------------------------------
FrameTimeline LoadFrameTimeline(int capacity, double frameStepMs)
{
    FrameTimeline timeline = { 0 };
    timeline.capacity = (capacity > 0)? capacity : 1;
    timeline.frames = (FrameSample *)RL_CALLOC(timeline.capacity, sizeof(FrameSample));
    timeline.count = 0;
    timeline.frameStepMs = (frameStepMs > 0.0)? frameStepMs : 16.6667;
    timeline.frameStepKey = 0;
    return timeline;
}

void UnloadFrameTimeline(FrameTimeline *timeline)
{
    if ((timeline != NULL) && (timeline->frames != NULL))
    {
        RL_FREE(timeline->frames);
        timeline->frames = NULL;
        timeline->count = 0;
        timeline->capacity = 0;
    }
}

void FrAppendSample(FrameTimeline *timeline, FrameSample sample)
{
    if (timeline->count >= timeline->capacity)
    {
        // Grow geometrically; frame numbering stays dense because we never leave holes.
        int newCapacity = timeline->capacity*2;
        FrameSample *grown = (FrameSample *)RL_CALLOC(newCapacity, sizeof(FrameSample));
        for (int i = 0; i < timeline->count; i++) grown[i] = timeline->frames[i];
        RL_FREE(timeline->frames);
        timeline->frames = grown;
        timeline->capacity = newCapacity;
    }

    timeline->frames[timeline->count] = sample;
    timeline->count++;
}

const FrameSample *FrGetFrame(const FrameTimeline *timeline, int frame)
{
    if ((timeline == NULL) || (timeline->count == 0)) return NULL;
    if (frame < 0) frame = 0;
    if (frame >= timeline->count) frame = timeline->count - 1;
    // Direct indexed read: O(1), no counters advanced, no side effects.
    return &timeline->frames[frame];
}

int FrClampFrame(const FrameTimeline *timeline, int frame)
{
    if (timeline->count == 0) return 0;
    if (frame < 0) return 0;
    if (frame >= timeline->count) return timeline->count - 1;
    return frame;
}

//----------------------------------------------------------------------------------
// Frame-derived hold duration
//----------------------------------------------------------------------------------
double FrComputeHoldMs(const FrameSample *cur, const FrameSample *holdStart, double frameStepMs)
{
    if ((cur == NULL) || (holdStart == NULL)) return 0.0;
    if (!cur->buttonDown) return 0.0;

    // Hold is the timestamp difference inside the recorded timeline. Because both timestamps
    // belong to frames, pausing on a frame changes nothing: the difference is fixed data.
    double duration = cur->timeMs - holdStart->timeMs;
    if (duration < 0.0) duration = 0.0;

    (void)frameStepMs;  // Timestamp difference already encodes the number of held frames
    return duration;
}

//----------------------------------------------------------------------------------
// Deterministic sample construction (edges + deltas + hold are computed once, here)
//----------------------------------------------------------------------------------
FrameSample FrBuildSample(int frame, double timeMs,
                          bool buttonDown, int key, bool keyDown,
                          const Vector2 *positions, int pointCount,
                          const FrameSample *prev, double frameStepMs)
{
    FrameSample s = { 0 };
    s.frame = frame;
    s.timeMs = timeMs;
    s.key = key;
    s.keyDown = keyDown;
    s.buttonDown = buttonDown;

    // Edges are derived strictly from this frame vs the previous frame. Seeking back and
    // rebuilding never re-fires an edge because the previous frame is fixed recorded data.
    bool prevButton = (prev != NULL)? prev->buttonDown : false;
    s.buttonPressed = buttonDown && !prevButton;
    s.buttonReleased = !buttonDown && prevButton;

    bool prevKey = (prev != NULL)? prev->keyDown : false;
    s.keyPressed = keyDown && !prevKey;

    // Copy finger points and compute per-frame deltas against the previous frame's positions.
    for (int i = 0; i < FR_MAX_POINTS; i++)
    {
        s.points[i].active = (i < pointCount) && (positions != NULL);
        if (s.points[i].active)
        {
            s.points[i].position = positions[i];
            if ((prev != NULL) && prev->points[i].active)
            {
                s.points[i].delta.x = positions[i].x - prev->points[i].position.x;
                s.points[i].delta.y = positions[i].y - prev->points[i].position.y;
            }
            else
            {
                // First frame a finger appears: no movement yet.
                s.points[i].delta = (Vector2){ 0.0f, 0.0f };
            }
        }
        else
        {
            s.points[i].position = (Vector2){ 0.0f, 0.0f };
            s.points[i].delta = (Vector2){ 0.0f, 0.0f };
        }
    }

    // Classify and compute all gesture quantities from frame data (pure, no wall clock).
    s.gesture = FrClassifyGesture(&s, prev, frameStepMs);
    s.dragVector = (Vector2){ 0.0f, 0.0f };
    s.dragAngle = 0.0f;
    s.pinchVector = (Vector2){ 0.0f, 0.0f };
    s.pinchAngle = 0.0f;

    if (pointCount >= 2)
    {
        s.pinchVector.x = positions[1].x - positions[0].x;
        s.pinchVector.y = positions[1].y - positions[0].y;
        s.pinchAngle = 360.0f - FrVecAngle(positions[0], positions[1]);
    }

    if (buttonDown && (pointCount >= 1))
    {
        // Drag vector is this frame's movement; angle follows the cumulative travel direction.
        s.dragVector = s.points[0].delta;
        if ((prev != NULL) && prev->points[0].active)
        {
            s.dragAngle = 360.0f - FrVecAngle(prev->points[0].position, s.points[0].position);
        }
    }

    // Hold duration: walk is not needed at query time. We store the hold-start timestamp on
    // each frame via timeMs bookkeeping done by the caller timeline; here compute relative to
    // the most recent press frame using the previous sample's chain through timeMs.
    s.holdMs = 0.0;
    if (buttonDown)
    {
        if (s.buttonPressed || (prev == NULL) || !prev->buttonDown)
        {
            s.holdMs = 0.0;  // A press starts the hold at zero on its own frame
        }
        else
        {
            // Add exactly this frame's own slice of time. A manual 2-second pause between
            // steps is absent from timeMs, so it can never leak in.
            double step = timeMs - prev->timeMs;
            s.holdMs = prev->holdMs + (step > 0.0? step : frameStepMs);
        }
    }

    return s;
}

//----------------------------------------------------------------------------------
// Pure gesture classification over adjacent frames
//----------------------------------------------------------------------------------
int FrClassifyGesture(const FrameSample *cur, const FrameSample *prev, double frameStepMs)
{
    (void)frameStepMs;
    if (cur == NULL) return FR_GESTURE_NONE;

    int pointCount = 0;
    for (int i = 0; i < FR_MAX_POINTS; i++) if (cur->points[i].active) pointCount++;

    // Two fingers: pinch family. Direction comes from whether the fingers separated.
    if (pointCount >= 2)
    {
        if ((prev != NULL) && prev->points[0].active && prev->points[1].active)
        {
            float prevDist = FrVecDistance(prev->points[0].position, prev->points[1].position);
            float curDist = FrVecDistance(cur->points[0].position, cur->points[1].position);
            float movedA = FrVecDistance(prev->points[0].position, cur->points[0].position);
            float movedB = FrVecDistance(prev->points[1].position, cur->points[1].position);

            // A pinch only exists while the fingers move beyond the minimum threshold;
            // stationary two-finger frames stay HOLD so a frozen pinch never flips identity.
            if ((movedA >= FR_MINIMUM_PINCH) || (movedB >= FR_MINIMUM_PINCH))
            {
                if (curDist < prevDist) return FR_GESTURE_PINCH_IN;
                if (curDist > prevDist) return FR_GESTURE_PINCH_OUT;
            }
            return FR_GESTURE_HOLD;
        }
        // The frame the two fingers first land: not a pinch yet (mirrors rgestures DOWN).
        return cur->buttonDown? FR_GESTURE_HOLD : FR_GESTURE_NONE;
    }

    // Single finger / mouse.
    if (pointCount == 1)
    {
        // A press edge begins a contact. While the button stays held we report HOLD when
        // stationary and DRAG when moving; TAP/SWIPE are only decided at release, matching
        // rgestures where GESTURE_TAP is set on DOWN but immediately becomes HOLD if held.
        if (cur->buttonDown)
        {
            if (cur->buttonPressed)
            {
                // Distinguish a double tap only when a prior quick contact just ended.
                if ((prev != NULL) && prev->buttonReleased &&
                    ((cur->timeMs - prev->timeMs) <= FR_TAP_TIMEOUT_MS) &&
                    (FrVecDistance(prev->points[0].position, cur->points[0].position)
                     < FR_MINIMUM_DRAG*1000.0f))
                {
                    return FR_GESTURE_DOUBLETAP;
                }
                // First frame of a held press reads as HOLD (it is being held, not released).
                return FR_GESTURE_HOLD;
            }

            bool moved = (cur->points[0].delta.x != 0.0f) || (cur->points[0].delta.y != 0.0f);
            if (moved) return FR_GESTURE_DRAG;
            return FR_GESTURE_HOLD;
        }

        if (cur->buttonReleased && (prev != NULL) && prev->buttonDown)
        {
            float travel = FrVecDistance(prev->points[0].position, cur->points[0].position);

            // A fast, far release is a directional swipe.
            float dx = cur->points[0].position.x - prev->points[0].position.x;
            float dy = cur->points[0].position.y - prev->points[0].position.y;
            double elapsed = cur->timeMs - prev->timeMs;
            float speed = (elapsed > 0.0)? (travel/(float)(elapsed/1000.0)) : 0.0f;

            if ((speed > FR_FORCE_TO_SWIPE*1000.0f) && (travel > 0.0f))
            {
                float angle = FrVecAngle(prev->points[0].position, cur->points[0].position);
                if ((angle < 30.0f) || (angle > 330.0f)) return FR_GESTURE_SWIPE_RIGHT;
                if ((angle >= 30.0f) && (angle <= 150.0f)) return FR_GESTURE_SWIPE_DOWN;
                if ((angle > 150.0f) && (angle < 210.0f)) return FR_GESTURE_SWIPE_LEFT;
                if ((angle >= 210.0f) && (angle <= 330.0f)) return FR_GESTURE_SWIPE_UP;
            }

            // Otherwise the just-ended stationary/short contact was a tap.
            (void)dx; (void)dy;
            return FR_GESTURE_TAP;
        }
    }

    return FR_GESTURE_NONE;
}

//----------------------------------------------------------------------------------
// Gesture naming
//----------------------------------------------------------------------------------
const char *FrGestureName(int gesture)
{
    switch (gesture)
    {
        case FR_GESTURE_TAP:         return "Tap";
        case FR_GESTURE_DOUBLETAP:   return "Double Tap";
        case FR_GESTURE_HOLD:        return "Hold";
        case FR_GESTURE_DRAG:        return "Drag";
        case FR_GESTURE_SWIPE_RIGHT: return "Swipe Right";
        case FR_GESTURE_SWIPE_LEFT:  return "Swipe Left";
        case FR_GESTURE_SWIPE_UP:    return "Swipe Up";
        case FR_GESTURE_SWIPE_DOWN:  return "Swipe Down";
        case FR_GESTURE_PINCH_IN:    return "Pinch In";
        case FR_GESTURE_PINCH_OUT:   return "Pinch Out";
        case FR_GESTURE_NONE:
        default:                     return "None";
    }
}

//----------------------------------------------------------------------------------
// Backend-agnostic canvas: one entry point, two concrete routing targets
//----------------------------------------------------------------------------------
void FrCanvasClear(FrCanvas canvas, Color color)
{
    if (canvas.target != NULL) ImageClearBackground(canvas.target, color);
    else ClearBackground(color);
}

void FrCanvasCircle(FrCanvas canvas, Vector2 center, float radius, Color color)
{
    if (canvas.target != NULL) ImageDrawCircleV(canvas.target, center, (int)radius, color);
    else DrawCircleV(center, radius, color);
}

void FrCanvasLine(FrCanvas canvas, Vector2 a, Vector2 b, float thickness, Color color)
{
    if (canvas.target != NULL) ImageDrawLineEx(canvas.target, a, b, (int)thickness, color);
    else DrawLineEx(a, b, thickness, color);
}

void FrCanvasText(FrCanvas canvas, const char *text, int x, int y, int fontSize, Color color)
{
    if (canvas.target != NULL) ImageDrawText(canvas.target, text, x, y, fontSize, color);
    else DrawText(text, x, y, fontSize, color);
}

//----------------------------------------------------------------------------------
// Frame rendering (identical geometry for both backends)
//----------------------------------------------------------------------------------
void FrRenderFrame(FrCanvas canvas, const FrameSample *s, int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;

    FrCanvasClear(canvas, RAYWHITE);

    // Header: frame number and gesture name, fixed position in both outputs.
    char header[FR_MAX_TEXT*2] = { 0 };
    snprintf(header, sizeof(header), "Frame %i - %s", s->frame, FrGestureName(s->gesture));
    FrCanvasText(canvas, header, 12, 10, 20, DARKGRAY);

    // Keyboard state on its own line under the header so it never collides on narrow screens.
    if (s->keyDown)
    {
        char key[32] = { 0 };
        snprintf(key, sizeof(key), "key %i %s", s->key, s->keyPressed? "pressed" : "held");
        FrCanvasText(canvas, key, 12, 36, 16, DARKGREEN);
    }

    // Draw each active finger. Two fingers also get a connecting line for pinch review.
    for (int i = 0; i < FR_MAX_POINTS; i++)
    {
        if (!s->points[i].active) continue;

        Color fingerColor = (i == 0)?
            (s->buttonDown? RED : MAROON) :
            (s->buttonDown? ORANGE : GOLD);

        // Soft halo + solid dot, same radii on window and image.
        FrCanvasCircle(canvas, s->points[i].position, 14.0f, Fade(fingerColor, 0.25f));
        FrCanvasCircle(canvas, s->points[i].position, 6.0f, fingerColor);
    }

    if (s->points[0].active && s->points[1].active)
    {
        FrCanvasLine(canvas, s->points[0].position, s->points[1].position, 3.0f, VIOLET);
    }

    // The required label next to the pointer: gesture name plus hold milliseconds, placed
    // below the dot so it never overlaps the header regardless of pointer height.
    if (s->points[0].active)
    {
        int lx = (int)s->points[0].position.x + 14;
        int ly = (int)s->points[0].position.y + 18;

        char label[FR_MAX_TEXT*2] = { 0 };
        snprintf(label, sizeof(label), "%s hold=%ims", FrGestureName(s->gesture), (int)(s->holdMs + 0.5));
        FrCanvasText(canvas, label, lx, ly, 18, BLACK);

        // Per-frame pointer delta, shown so a stationary frozen frame reads 0,0 every time.
        char delta[48] = { 0 };
        snprintf(delta, sizeof(delta), "dx=%d dy=%d",
                 (int)s->points[0].delta.x, (int)s->points[0].delta.y);
        FrCanvasText(canvas, delta, lx, ly + 22, 14, GRAY);
    }

    // Pinch readout stays fixed while a pinch frame is frozen.
    if (s->points[0].active && s->points[1].active)
    {
        float pinchDist = FrVecDistance(s->points[0].position, s->points[1].position);
        char pinch[64] = { 0 };
        snprintf(pinch, sizeof(pinch), "pinch dist=%.1f angle=%.1f", pinchDist, s->pinchAngle);
        FrCanvasText(canvas, pinch, 12, screenHeight - 28, 16, PURPLE);
    }
}

Image FrRenderFrameToImage(const FrameTimeline *timeline, int frame, int width, int height)
{
    const FrameSample *s = FrGetFrame(timeline, frame);
    Image image = GenImageColor(width, height, RAYWHITE);

    if (s != NULL)
    {
        FrCanvas canvas = { 0 };
        canvas.target = &image;       // Route every primitive to the RAM image
        FrRenderFrame(canvas, s, width, height);
    }

    return image;
}
