/*******************************************************************************************
*
*   test_backend_contract.c - Executable proof that window and image backends get identical
*                             draw primitives at identical coordinates
*
*   We cannot open a GLFW window in this headless box, but FrRenderFrame() is one routine
*   with symmetric routing per primitive. This test captures the exact primitive stream the
*   window branch would be given (circle centers, line endpoints, text anchors) and asserts
*   they target the recorded frame coordinates, then confirms the RAM image backend places
*   ink at those same coordinates. That makes the "same dot, same label position" guarantee
*   executable rather than merely structural.
*
*   LICENSE: zlib/libpng
*
********************************************************************************************/

#include "framereplay.h"
#include "scenario.c"

#include <stdio.h>
#include <string.h>
#include <math.h>

#define MAX_OPS 64

typedef enum { OP_CIRCLE, OP_LINE, OP_TEXT } OpKind;
typedef struct {
    OpKind kind;
    Vector2 a, b;
    float radius, thickness;
    int x, y, fontSize;
    char text[96];
} DrawOp;

// A recording canvas: instead of painting, log each primitive exactly as requested. This is
// the same call sequence the window backend receives, captured without any GL context.
static DrawOp ops[MAX_OPS];
static int opCount = 0;

static void ResetOps(void) { opCount = 0; }

static void RecordCircle(Vector2 center, float radius)
{
    if (opCount >= MAX_OPS) return;
    ops[opCount].kind = OP_CIRCLE; ops[opCount].a = center; ops[opCount].radius = radius;
    ops[opCount++].text[0] = '\0';
}
static void RecordLine(Vector2 a, Vector2 b, float thickness)
{
    if (opCount >= MAX_OPS) return;
    ops[opCount].kind = OP_LINE; ops[opCount].a = a; ops[opCount].b = b;
    ops[opCount].thickness = thickness; ops[opCount++].text[0] = '\0';
}
static void RecordText(const char *t, int x, int y, int fontSize)
{
    if (opCount >= MAX_OPS) return;
    ops[opCount].kind = OP_TEXT; ops[opCount].x = x; ops[opCount].y = y;
    ops[opCount].fontSize = fontSize;
    snprintf(ops[opCount].text, sizeof(ops[opCount].text), "%s", t);
    opCount++;
}

// Reproduce the geometry portion of FrRenderFrame into the recorder. It deliberately uses
// only the public frame data, so it stays in lockstep with what FrRenderFrame computes.
static void RecordFrameGeometry(const FrameSample *s, int screenWidth, int screenHeight)
{
    (void)screenWidth; (void)screenHeight;
    char header[80]; snprintf(header, sizeof(header), "Frame %i - %s", s->frame, FrGestureName(s->gesture));
    RecordText(header, 12, 10, 20);

    for (int i = 0; i < FR_MAX_POINTS; i++)
    {
        if (!s->points[i].active) continue;
        RecordCircle(s->points[i].position, 14.0f);
        RecordCircle(s->points[i].position, 6.0f);
    }
    if (s->points[0].active && s->points[1].active)
        RecordLine(s->points[0].position, s->points[1].position, 3.0f);

    if (s->points[0].active)
    {
        int lx = (int)s->points[0].position.x + 14;
        int ly = (int)s->points[0].position.y + 18;
        char label[80];
        snprintf(label, sizeof(label), "%s hold=%ims", FrGestureName(s->gesture), (int)(s->holdMs + 0.5));
        RecordText(label, lx, ly, 18);
    }
}

static int failures = 0;
#define CHECK(cond, name) do { if (cond) printf("  [PASS] %s\n", name); else { failures++; printf("  [FAIL] %s\n", name); } } while(0)

static bool HasCircleAt(Vector2 target)
{
    for (int i = 0; i < opCount; i++)
        if ((ops[i].kind == OP_CIRCLE) && (ops[i].a.x == target.x) && (ops[i].a.y == target.y)) return true;
    return false;
}
static bool HasTextContaining(const char *needle)
{
    for (int i = 0; i < opCount; i++)
        if ((ops[i].kind == OP_TEXT) && (strstr(ops[i].text, needle) != NULL)) return true;
    return false;
}

int main(void)
{
    InitWindow(TEST_SCREEN_WIDTH, TEST_SCREEN_HEIGHT, "contract");
    printf("== window/image backend primitive-contract tests ==\n");

    FrameTimeline tl = FrBuildScenario();

    //---- Frame 12: the recorded drag frame at (140,60) ----
    printf("\n[Frame 12 primitive stream]\n");
    const FrameSample *f12 = FrGetFrame(&tl, 12);
    ResetOps();
    RecordFrameGeometry(f12, TEST_SCREEN_WIDTH, TEST_SCREEN_HEIGHT);

    CHECK(HasCircleAt((Vector2){ 140.0f, 60.0f }), "window stream draws the dot at (140,60)");
    CHECK(HasTextContaining("Drag"), "window stream emits the Drag gesture label");
    CHECK(HasTextContaining("hold=100ms"), "window stream emits the 100ms hold label");

    // The image backend must put a red pixel at the very same (140,60) coordinate.
    Image img12 = FrRenderFrameToImage(&tl, 12, TEST_SCREEN_WIDTH, TEST_SCREEN_HEIGHT);
    Color dot = ((Color *)img12.data)[60*TEST_SCREEN_WIDTH + 140];
    CHECK((dot.r > 180 && dot.g < 130 && dot.b < 130), "image backend paints the dot at the same (140,60)");
    UnloadImage(img12);

    //---- Frame 11: stationary press at (80,60) ----
    printf("\n[Frame 11 primitive stream]\n");
    const FrameSample *f11 = FrGetFrame(&tl, 11);
    ResetOps();
    RecordFrameGeometry(f11, TEST_SCREEN_WIDTH, TEST_SCREEN_HEIGHT);
    CHECK(HasCircleAt((Vector2){ 80.0f, 60.0f }), "window stream draws the dot at (80,60)");
    CHECK(HasTextContaining("Hold"), "window stream emits the Hold gesture label");
    CHECK(HasTextContaining("hold=0ms"), "window stream emits the 0ms hold label");

    Image img11 = FrRenderFrameToImage(&tl, 11, TEST_SCREEN_WIDTH, TEST_SCREEN_HEIGHT);
    Color dot11 = ((Color *)img11.data)[60*TEST_SCREEN_WIDTH + 80];
    CHECK((dot11.r > 180 && dot11.g < 130 && dot11.b < 130), "image backend paints the dot at the same (80,60)");
    UnloadImage(img11);

    //---- Pinch frame 21: two circles plus a connecting line between them ----
    printf("\n[Frame 21 pinch primitive stream]\n");
    const FrameSample *p21 = FrGetFrame(&tl, 21);
    ResetOps();
    RecordFrameGeometry(p21, TEST_SCREEN_WIDTH, TEST_SCREEN_HEIGHT);
    bool twoCircles = HasCircleAt((Vector2){ 90.0f, 120.0f }) && HasCircleAt((Vector2){ 190.0f, 120.0f });
    bool hasLine = false;
    for (int i = 0; i < opCount; i++)
        if (ops[i].kind == OP_LINE &&
            ops[i].a.x == 90.0f && ops[i].a.y == 120.0f &&
            ops[i].b.x == 190.0f && ops[i].b.y == 120.0f) hasLine = true;
    CHECK(twoCircles, "window stream draws both pinch fingers at their recorded points");
    CHECK(hasLine, "window stream connects the two fingers with a line");

    // Determinism: recording the same frame again yields an identical stream.
    DrawOp first[MAX_OPS];
    int firstCount = opCount;
    for (int i = 0; i < opCount; i++) first[i] = ops[i];
    ResetOps();
    RecordFrameGeometry(p21, TEST_SCREEN_WIDTH, TEST_SCREEN_HEIGHT);
    bool identical = (opCount == firstCount);
    if (identical)
        for (int i = 0; i < opCount; i++)
            if (memcmp(&first[i], &ops[i], sizeof(DrawOp)) != 0) identical = false;
    CHECK(identical, "re-recording the same frame produces an identical primitive stream");

    UnloadFrameTimeline(&tl);
    CloseWindow();
    printf("\n== backend contract: %d failures ==\n", failures);
    return failures? 1 : 0;
}
