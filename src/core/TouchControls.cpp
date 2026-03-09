#include "common.h"

#ifdef TOUCH_CONTROLS

#include "TouchControls.h"
#include "Pad.h"
#include "Frontend.h"
#include "Camera.h"
#include "Sprite2d.h"
#include "Font.h"
#include "main.h"

#include <math.h>

// Default configuration
float TouchControls::ms_stickRadius     = 80.0f;   // game pixels
float TouchControls::ms_stickDeadzone   = 0.15f;   // 15% deadzone
float TouchControls::ms_lookSensitivity = 2.5f;    // mouse sensitivity multiplier
float TouchControls::ms_stickBaseAlpha  = 80.0f;   // semi-transparent base
float TouchControls::ms_stickThumbAlpha = 160.0f;  // more opaque thumb

bool TouchControls::ms_enabled     = true;
bool TouchControls::ms_initialized = false;

TouchControls::TouchPoint TouchControls::ms_touches[TOUCH_MAX_POINTS];
TouchControls::StickVisual TouchControls::ms_stickVisual;

float TouchControls::ms_lookDeltaX = 0.0f;
float TouchControls::ms_lookDeltaY = 0.0f;

bool   TouchControls::ms_menuLMB            = false;
bool   TouchControls::ms_menuLMBPending     = false;
bool   TouchControls::ms_menuPressConsumed  = false;
bool   TouchControls::ms_menuReleaseQueued  = false;
double TouchControls::ms_menuCursorX        = 0.0;
double TouchControls::ms_menuCursorY        = 0.0;
bool   TouchControls::ms_menuCursorValid    = false;

void
TouchControls::Init(void)
{
	Reset();
	ms_initialized = true;
}

void
TouchControls::Shutdown(void)
{
	Reset();
	ms_initialized = false;
}

void
TouchControls::Reset(void)
{
	for (int i = 0; i < TOUCH_MAX_POINTS; i++) {
		ms_touches[i].active = false;
		ms_touches[i].index  = -1;
		ms_touches[i].zone   = ZONE_NONE;
		ms_touches[i].hasPrev = false;
	}
	ms_stickVisual.visible = false;
	ms_lookDeltaX = 0.0f;
	ms_lookDeltaY = 0.0f;
	ms_menuLMB = false;
	ms_menuLMBPending = false;
	ms_menuPressConsumed = false;
	ms_menuReleaseQueued = false;
	ms_menuCursorValid = false;
}

TouchControls::eTouchZone
TouchControls::ClassifyZone(double x, double y)
{
	// Left half of screen = stick zone, right half = look zone
	float halfScreen = SCREEN_WIDTH * 0.5f;
	
	if (x < halfScreen)
		return ZONE_LEFT_STICK;
	else
		return ZONE_RIGHT_LOOK;
}

TouchControls::TouchPoint*
TouchControls::FindTouchByIndex(int touchIndex)
{
	for (int i = 0; i < TOUCH_MAX_POINTS; i++) {
		if (ms_touches[i].active && ms_touches[i].index == touchIndex)
			return &ms_touches[i];
	}
	return nil;
}

TouchControls::TouchPoint*
TouchControls::FindFreeTouchSlot(void)
{
	for (int i = 0; i < TOUCH_MAX_POINTS; i++) {
		if (!ms_touches[i].active)
			return &ms_touches[i];
	}
	return nil;
}

TouchControls::TouchPoint*
TouchControls::FindTouchByZone(eTouchZone zone)
{
	for (int i = 0; i < TOUCH_MAX_POINTS; i++) {
		if (ms_touches[i].active && ms_touches[i].zone == zone)
			return &ms_touches[i];
	}
	return nil;
}

void
TouchControls::HandleTouchDown(int touchIndex, double x, double y)
{
	// fprintf(stderr, "[TOUCH DIAG] HandleTouchDown idx=%d x=%.0f y=%.0f menu=%d\n",
	// 	touchIndex, x, y, FrontEndMenuManager.m_bMenuActive);

	if (!ms_enabled)
		return;

	// Menu mode: latch LMB and set cursor position
	if (FrontEndMenuManager.m_bMenuActive) {
		ms_menuCursorX = x;
		ms_menuCursorY = y;
		ms_menuCursorValid = true;
		// Don't set LMB immediately — delay by 1 frame so cursor moves first
		ms_menuLMBPending = true;
		ms_menuLMB = false;
		ms_menuPressConsumed = false;
		ms_menuReleaseQueued = false;

		FrontEndMenuManager.m_nMouseTempPosX = (int32)x;
		FrontEndMenuManager.m_nMouseTempPosY = (int32)y;
		return;
	}

	// Gameplay mode: allocate touch slot
	TouchPoint *tp = FindFreeTouchSlot();
	if (tp == nil)
		return;

	eTouchZone zone = ClassifyZone(x, y);

	// Only one touch per zone
	if (FindTouchByZone(zone) != nil) {
		if (zone == ZONE_LEFT_STICK)
			return;
	}

	tp->active = true;
	tp->index  = touchIndex;
	tp->x      = x;
	tp->y      = y;
	tp->startX = x;
	tp->startY = y;
	tp->prevX  = x;
	tp->prevY  = y;
	tp->hasPrev = false;
	tp->zone   = zone;

	if (zone == ZONE_LEFT_STICK) {
		ms_stickVisual.visible = true;
		ms_stickVisual.baseX   = (float)x;
		ms_stickVisual.baseY   = (float)y;
		ms_stickVisual.thumbX  = (float)x;
		ms_stickVisual.thumbY  = (float)y;
		ms_stickVisual.radius  = ms_stickRadius;
	}
}

void
TouchControls::HandleTouchMove(int touchIndex, double x, double y)
{
	if (!ms_enabled)
		return;

	// Menu mode: just update cursor
	if (FrontEndMenuManager.m_bMenuActive) {
		ms_menuCursorX = x;
		ms_menuCursorY = y;
		ms_menuCursorValid = true;
		FrontEndMenuManager.m_nMouseTempPosX = (int32)x;
		FrontEndMenuManager.m_nMouseTempPosY = (int32)y;
		return;
	}

	// Gameplay mode
	TouchPoint *tp = FindTouchByIndex(touchIndex);
	if (tp == nil)
		return;

	tp->prevX = tp->x;
	tp->prevY = tp->y;
	tp->x     = x;
	tp->y     = y;

	if (tp->zone == ZONE_LEFT_STICK) {
		float dx = (float)(x - tp->startX);
		float dy = (float)(y - tp->startY);
		float dist = sqrtf(dx * dx + dy * dy);

		if (dist > ms_stickRadius) {
			dx = dx / dist * ms_stickRadius;
			dy = dy / dist * ms_stickRadius;
		}

		ms_stickVisual.thumbX = ms_stickVisual.baseX + dx;
		ms_stickVisual.thumbY = ms_stickVisual.baseY + dy;
	}
	else if (tp->zone == ZONE_RIGHT_LOOK) {
		if (tp->hasPrev) {
			ms_lookDeltaX += (float)(x - tp->prevX) * ms_lookSensitivity;
			ms_lookDeltaY += (float)(tp->prevY - y) * ms_lookSensitivity;
		}
	}

	tp->hasPrev = true;
}

void
TouchControls::HandleTouchUp(int touchIndex, double x, double y)
{
	if (!ms_enabled)
		return;

	// Menu mode: queue release (will fire after press is consumed)
	if (FrontEndMenuManager.m_bMenuActive) {
		ms_menuReleaseQueued = true;
		return;
	}

	// Gameplay mode
	TouchPoint *tp = FindTouchByIndex(touchIndex);
	if (tp == nil)
		return;

	if (tp->zone == ZONE_LEFT_STICK)
		ms_stickVisual.visible = false;

	tp->active  = false;
	tp->index   = -1;
	tp->zone    = ZONE_NONE;
	tp->hasPrev = false;
}

void
TouchControls::ApplyToJoyState(void)
{
	if (!ms_enabled)
		return;

	// Don't apply stick in menus
	if (FrontEndMenuManager.m_bMenuActive)
		return;

	TouchPoint *stickTouch = FindTouchByZone(ZONE_LEFT_STICK);
	if (stickTouch == nil)
		return;

	float dx = (float)(stickTouch->x - stickTouch->startX);
	float dy = (float)(stickTouch->y - stickTouch->startY);
	float dist = sqrtf(dx * dx + dy * dy);

	if (dist < ms_stickRadius * ms_stickDeadzone)
		return;  // inside deadzone

	// Normalize to -1..1 range
	float maxDist = ms_stickRadius;
	float nx = dx / maxDist;
	float ny = dy / maxDist;
	
	// Clamp
	if (nx > 1.0f) nx = 1.0f;
	if (nx < -1.0f) nx = -1.0f;
	if (ny > 1.0f) ny = 1.0f;
	if (ny < -1.0f) ny = -1.0f;

	// Apply deadzone remapping: remap [deadzone..1] to [0..1]
	float magnitude = sqrtf(nx * nx + ny * ny);
	if (magnitude > 0.0f && magnitude > ms_stickDeadzone) {
		float remapped = (magnitude - ms_stickDeadzone) / (1.0f - ms_stickDeadzone);
		if (remapped > 1.0f) remapped = 1.0f;
		nx = nx / magnitude * remapped;
		ny = ny / magnitude * remapped;
	}

	// Write to pad - PCTempJoyState uses int16 range ±128
	CPad *pad = CPad::GetPad(0);
	pad->PCTempJoyState.LeftStickX = (int16)(nx * 128.0f);
	pad->PCTempJoyState.LeftStickY = (int16)(ny * 128.0f);
}

void
TouchControls::ApplyToMouse(float &outDeltaX, float &outDeltaY, bool &outLMB, bool &outConsumed)
{
	outConsumed = false;
	outLMB = false;

	if (!ms_enabled)
		return;

	// --- Menu mode ---
	if (FrontEndMenuManager.m_bMenuActive) {
		// Always push cursor position
		if (ms_menuCursorValid) {
			FrontEndMenuManager.m_nMouseTempPosX = (int32)ms_menuCursorX;
			FrontEndMenuManager.m_nMouseTempPosY = (int32)ms_menuCursorY;
			outConsumed = true;
		}

		// Frame 1 after press: cursor moved, now arm LMB for next frame
		if (ms_menuLMBPending) {
			ms_menuLMBPending = false;
			ms_menuLMB = true;
			// outLMB stays false this frame — cursor settles first
			return;
		}

		// Frame 2+: LMB is active
		if (ms_menuLMB) {
			outLMB = true;
			outConsumed = true;
			ms_menuPressConsumed = true;

			if (ms_menuReleaseQueued) {
				ms_menuLMB = false;
				ms_menuReleaseQueued = false;
				ms_menuCursorValid = false;
			}
		}
		return;
	}

	// --- Gameplay mode ---
	if (ms_lookDeltaX != 0.0f || ms_lookDeltaY != 0.0f) {
		outDeltaX = ms_lookDeltaX;
		outDeltaY = ms_lookDeltaY;
		outConsumed = true;

		ms_lookDeltaX = 0.0f;
		ms_lookDeltaY = 0.0f;
	}
}

// ---- Drawing ----

void
TouchControls::DrawCircle(float cx, float cy, float radius, int segments,
                            uint8 r, uint8 g, uint8 b, uint8 a)
{
	// Draw a circle outline using line segments via Im2D
	// We approximate with CSprite2d::DrawRect for thin rectangles at each segment
	// Actually, let's use a simple approach: draw small rectangles along the circumference
	
	float angleStep = 2.0f * 3.14159265f / (float)segments;
	float thickness = SCREEN_SCALE_X(2.0f);
	
	for (int i = 0; i < segments; i++) {
		float a0 = angleStep * i;
		float a1 = angleStep * (i + 1);
		
		float x0 = cx + cosf(a0) * radius;
		float y0 = cy + sinf(a0) * radius;
		float x1 = cx + cosf(a1) * radius;
		float y1 = cy + sinf(a1) * radius;
		
		// Draw a thin rect between the two points
		// Approximate as a small rect
		float midX = (x0 + x1) * 0.5f;
		float midY = (y0 + y1) * 0.5f;
		float halfT = thickness * 0.5f;
		
		CSprite2d::DrawRect(
			CRect(Min(x0, x1) - halfT, Min(y0, y1) - halfT,
			      Max(x0, x1) + halfT, Max(y0, y1) + halfT),
			CRGBA(r, g, b, a));
	}
}

void
TouchControls::DrawFilledCircle(float cx, float cy, float radius, int segments,
                                 uint8 r, uint8 g, uint8 b, uint8 a)
{
	// Approximate filled circle with concentric rectangles (simple approach)
	// or use triangle fan via Im2D
	
	// Simple approach: draw overlapping rects to approximate
	// Better approach: use RwIm2D triangle fan
	float nearZ = RwIm2DGetNearScreenZ();
	float recipZ = 1.0f / RwCameraGetNearClipPlane(Scene.camera);
	
	// Triangle fan: center + (segments+1) vertices
	int numVerts = segments + 2; // center + ring + closing
	if (numVerts > 102) numVerts = 102; // safety cap
	
	RwIm2DVertex verts[102];
	float angleStep = 2.0f * 3.14159265f / (float)segments;
	
	// Center vertex
	RwIm2DVertexSetScreenX(&verts[0], cx);
	RwIm2DVertexSetScreenY(&verts[0], cy);
	RwIm2DVertexSetScreenZ(&verts[0], nearZ);
	RwIm2DVertexSetRecipCameraZ(&verts[0], recipZ);
	RwIm2DVertexSetIntRGBA(&verts[0], r, g, b, a);
	
	for (int i = 0; i <= segments; i++) {
		float angle = angleStep * i;
		float px = cx + cosf(angle) * radius;
		float py = cy + sinf(angle) * radius;
		
		RwIm2DVertexSetScreenX(&verts[i + 1], px);
		RwIm2DVertexSetScreenY(&verts[i + 1], py);
		RwIm2DVertexSetScreenZ(&verts[i + 1], nearZ);
		RwIm2DVertexSetRecipCameraZ(&verts[i + 1], recipZ);
		RwIm2DVertexSetIntRGBA(&verts[i + 1], r, g, b, a);
	}
	
	// Render as triangle fan
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nil);
	RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDINVSRCALPHA);
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
	
	RwIm2DRenderPrimitive(rwPRIMTYPETRIFAN, verts, numVerts);
	
	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
	RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
}

void
TouchControls::Draw(void)
{
	if (!ms_enabled || !ms_initialized)
		return;

	// Don't draw controls in menus
	if (FrontEndMenuManager.m_bMenuActive)
		return;

	// Draw the left stick if it's visible (finger is touching left zone)
	if (ms_stickVisual.visible) {
		float baseR = ms_stickVisual.radius;
		float thumbR = SCREEN_SCALE_X(25.0f);
		
		// Draw base circle (outer ring area)
		DrawFilledCircle(
			ms_stickVisual.baseX, ms_stickVisual.baseY,
			baseR,
			32,
			255, 255, 255, (uint8)ms_stickBaseAlpha);
		
		// Draw thumb circle
		DrawFilledCircle(
			ms_stickVisual.thumbX, ms_stickVisual.thumbY,
			thumbR,
			24,
			255, 255, 255, (uint8)ms_stickThumbAlpha);
	}
	else {
		// When stick is not active, draw a subtle hint on the left side
		float hintX = SCREEN_SCALE_X(120.0f);
		float hintY = SCREEN_HEIGHT - SCREEN_SCALE_Y(120.0f);
		float hintR = SCREEN_SCALE_X(40.0f);
		
		DrawFilledCircle(hintX, hintY, hintR, 24, 255, 255, 255, 30);
	}
}

#endif // TOUCH_CONTROLS
