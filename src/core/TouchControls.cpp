#include "common.h"

#ifdef TOUCH_CONTROLS

#include "TouchControls.h"
#include "Pad.h"
#include "Frontend.h"
#include "Camera.h"
#include "Sprite2d.h"
#include "Font.h"
#include "main.h"
#include "CutsceneMgr.h"
#include "OffscreenRenderer.h"

#include "../extras/imgui/imgui.h"
#include "../extras/imgui/backends/imgui_impl_glfw.h"
#include "../extras/imgui/backends/imgui_impl_opengl3.h"

#include <math.h>

// Default configuration
float TouchControls::ms_stickRadius     = 80.0f;   // game pixels
float TouchControls::ms_stickDeadzone   = 0.15f;   // 15% deadzone
float TouchControls::ms_lookSensitivity = 7.5f;    // mouse sensitivity multiplier
float TouchControls::ms_stickBaseAlpha  = 80.0f;   // semi-transparent base
float TouchControls::ms_stickThumbAlpha = 160.0f;  // more opaque thumb

float TouchControls::ms_mmPerGamePixelX = 0.1f;  // fallback ~10 px/mm
float TouchControls::ms_mmPerGamePixelY = 0.1f;
float TouchControls::ms_pixelAspect     = 1.0f;

bool TouchControls::ms_enabled     = true;

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

TouchButton TouchControls::ms_buttons[TOUCH_MAX_BUTTONS];
int    TouchControls::ms_numButtons    = 0;
uint32 TouchControls::ms_currentLayout = TOUCH_LAYOUT_NONE;

// Cached geometry for optimized circle drawing
float TouchControls::ms_unitCircleX[CIRCLE_SEGMENTS + 1];
float TouchControls::ms_unitCircleY[CIRCLE_SEGMENTS + 1];
RwIm2DVertex TouchControls::ms_circleVerts[CIRCLE_SEGMENTS + 2];
float TouchControls::ms_cachedNearZ = 0.0f;
float TouchControls::ms_cachedRecipZ = 1.0f;
bool  TouchControls::ms_renderStateSet = false;

bool TouchControls::ms_imguiInitialized = false;

// ============================================================
// Helper: add a button to the array
// ============================================================

static int AddButton(TouchButton *arr, int &count,
                     const char *label, uint32 layout,
                     eTouchAnchor anchor, float ox, float oy, float w, float h,
                     eTouchActionType atype, int32 acode,
                     uint8 r, uint8 g, uint8 b, uint8 normA, uint8 pressA,
                     bool round, bool allowLook = false)
{
	if (count >= TOUCH_MAX_BUTTONS) return -1;
	TouchButton &btn = arr[count];
	btn.label        = label;
	btn.layoutFlags  = layout;
	btn.anchor       = anchor;
	btn.offsetX      = ox;
	btn.offsetY      = oy;
	btn.width        = w;
	btn.height       = h;
	btn.actionType   = atype;
	btn.actionCode   = acode;
	btn.bgR = r; btn.bgG = g; btn.bgB = b;
	btn.normalAlpha  = normA;
	btn.pressedAlpha = pressA;
	btn.roundVisual  = round;
	btn.allowLook    = allowLook;
	btn.ClearState();
	return count++;
}

// ============================================================
// Button definitions
// ============================================================

void
TouchControls::SetupButtons(void)
{
	ms_numButtons = 0;

	// ---- MENU layout ----
	// Back button (Triangle) — top-left
	AddButton(ms_buttons, ms_numButtons,
		"<",
		TOUCH_LAYOUT_MENU | TOUCH_LAYOUT_CUTSCENE,
		ANCHOR_TOP_LEFT, 15.0f, 15.0f,
		70.0f, 45.0f,
		TACTION_PAD, TPAD_TRIANGLE,        // Triangle = back in menu
		40, 40, 40, 120, 200,
		false);

	// Navigation UP — right side
	AddButton(ms_buttons, ms_numButtons,
		"^",                               // или "▲"
		TOUCH_LAYOUT_MENU,
		ANCHOR_BOTTOM_RIGHT, 30.0f, 180.0f,
		55.0f, 55.0f,
		TACTION_PAD, TPAD_DPAD_UP,
		50, 50, 50, 120, 200,
		true);                             // round

	// Navigation DOWN — right side
	AddButton(ms_buttons, ms_numButtons,
		"v",                               // или "▼"
		TOUCH_LAYOUT_MENU,
		ANCHOR_BOTTOM_RIGHT, 30.0f, 100.0f,
		55.0f, 55.0f,
		TACTION_PAD, TPAD_DPAD_DOWN,
		50, 50, 50, 120, 200,
		true);

	// Value LEFT — left side
	AddButton(ms_buttons, ms_numButtons,
		"<",
		TOUCH_LAYOUT_MENU,
		ANCHOR_BOTTOM_LEFT, 30.0f, 140.0f,
		55.0f, 55.0f,
		TACTION_PAD, TPAD_DPAD_LEFT,
		50, 50, 50, 120, 200,
		true);

	// Value RIGHT — left side
	AddButton(ms_buttons, ms_numButtons,
		">",
		TOUCH_LAYOUT_MENU,
		ANCHOR_BOTTOM_LEFT, 100.0f, 140.0f,
		55.0f, 55.0f,
		TACTION_PAD, TPAD_DPAD_RIGHT,
		50, 50, 50, 120, 200,
		true);

	// Confirm (Cross) — bottom center-right (optional, if tap doesn't work)
	AddButton(ms_buttons, ms_numButtons,
		"OK",
		TOUCH_LAYOUT_MENU,
		ANCHOR_BOTTOM_RIGHT, 100.0f, 100.0f,
		60.0f, 45.0f,
		TACTION_PAD, TPAD_CROSS,
		40, 80, 40, 120, 200,
		false);

	// ---- GAMEPLAY layout ----

	// Pause / Menu button — top-right
	AddButton(ms_buttons, ms_numButtons,
		"II",                              // label (pause icon)
		TOUCH_LAYOUT_GAMEPLAY,
		ANCHOR_TOP_LEFT, 15.0f, 15.0f,    // top-right corner
		50.0f, 50.0f,
		TACTION_PAD, TPAD_START,           // Start = открыть меню
		40, 40, 40, 100, 200,
		false);                            // rectangle

	// Sprint / Cross (A) — bottom-right area
	AddButton(ms_buttons, ms_numButtons,
		"A",
		TOUCH_LAYOUT_GAMEPLAY,
		ANCHOR_BOTTOM_RIGHT, 30.0f, 130.0f,
		55.0f, 55.0f,
		TACTION_PAD, TPAD_CROSS,
		50, 120, 50, 100, 200,
		true, true);

	// Jump / Square (X) — above Cross
	AddButton(ms_buttons, ms_numButtons,
		"X",
		TOUCH_LAYOUT_GAMEPLAY,
		ANCHOR_BOTTOM_RIGHT, 90.0f, 70.0f,
		55.0f, 55.0f,
		TACTION_PAD, TPAD_SQUARE,
		50, 50, 180, 100, 200,
		true, true);

	// Attack / Circle (B) — left of Cross
	AddButton(ms_buttons, ms_numButtons,
		"B",
		TOUCH_LAYOUT_GAMEPLAY,
		ANCHOR_BOTTOM_RIGHT, 90.0f, 190.0f,
		55.0f, 55.0f,
		TACTION_PAD, TPAD_CIRCLE,
		180, 50, 50, 100, 200,
		true, true);

	// Enter-vehicle / Triangle (Y) — above the cluster
	AddButton(ms_buttons, ms_numButtons,
		"Y",
		TOUCH_LAYOUT_GAMEPLAY,
		ANCHOR_BOTTOM_RIGHT, 30.0f, 250.0f,
		55.0f, 55.0f,
		TACTION_PAD, TPAD_TRIANGLE,
		180, 180, 50, 100, 200,
		true);

	// L1 — top-center, radio
	AddButton(ms_buttons, ms_numButtons,
		"RADIO",
		TOUCH_LAYOUT_GAMEPLAY,
		ANCHOR_TOP_CENTER, 0.0f, 15.0f,
		60.0f, 35.0f,
		TACTION_PAD, TPAD_L1,
		80, 80, 80, 90, 180,
		false);

	AddButton(ms_buttons, ms_numButtons,
		"$",                               // taxi/mission icon
		TOUCH_LAYOUT_GAMEPLAY,
		ANCHOR_BOTTOM_CENTER, 0.0f, 40.0f,
		55.0f, 55.0f,
		TACTION_PAD, TPAD_RIGHT_STICK,
		60, 180, 60, 120, 200,
		false);
}

// ============================================================
// Layout detection
// ============================================================

uint32
TouchControls::DetectLayout(void)
{
	if (FrontEndMenuManager.m_bMenuActive)
		return TOUCH_LAYOUT_MENU;

	if (CCutsceneMgr::IsRunning())
		return TOUCH_LAYOUT_CUTSCENE;

	return TOUCH_LAYOUT_GAMEPLAY;
}

void
TouchControls::UpdateLayout(void)
{
	uint32 newLayout = DetectLayout();

	// On layout change, clear all button states
	if (newLayout != ms_currentLayout) {
		for (int i = 0; i < ms_numButtons; i++)
			ms_buttons[i].ClearState();
		ms_currentLayout = newLayout;
	}

	// Recompute screen positions for active buttons
	for (int i = 0; i < ms_numButtons; i++) {
		TouchButton &btn = ms_buttons[i];
		if (!(btn.layoutFlags & ms_currentLayout))
			continue;

		float sw = SCREEN_SCALE_X(btn.width);
		float sh = SCREEN_SCALE_Y(btn.height);
		float ox = SCREEN_SCALE_X(btn.offsetX);
		float oy = SCREEN_SCALE_Y(btn.offsetY);

		switch (btn.anchor) {
		case ANCHOR_TOP_LEFT:
			btn.screenX = ox;
			btn.screenY = oy;
			break;
		case ANCHOR_TOP_RIGHT:
			btn.screenX = SCREEN_WIDTH - ox - sw;
			btn.screenY = oy;
			break;
		case ANCHOR_BOTTOM_LEFT:
			btn.screenX = ox;
			btn.screenY = SCREEN_HEIGHT - oy - sh;
			break;
		case ANCHOR_BOTTOM_RIGHT:
			btn.screenX = SCREEN_WIDTH - ox - sw;
			btn.screenY = SCREEN_HEIGHT - oy - sh;
			break;
		case ANCHOR_TOP_CENTER:
			btn.screenX = SCREEN_WIDTH * 0.5f - sw * 0.5f + ox;
			btn.screenY = oy;
			break;
		case ANCHOR_BOTTOM_CENTER:
			btn.screenX = SCREEN_WIDTH * 0.5f - sw * 0.5f + ox;
			btn.screenY = SCREEN_HEIGHT - oy - sh;
			break;
		}
		btn.screenW = sw;
		btn.screenH = sh;
	}
}

// ============================================================
// Init / Shutdown / Reset
// ============================================================

void
TouchControls::Init(void)
{
	if (ms_imguiInitialized) return;

	Reset();

	// Pre-calculate unit circle vertices (only once!)
	float angleStep = 2.0f * 3.14159265f / (float)CIRCLE_SEGMENTS;
	for (int i = 0; i <= CIRCLE_SEGMENTS; i++) {
		float angle = angleStep * i;
		ms_unitCircleX[i] = cosf(angle);
		ms_unitCircleY[i] = sinf(angle);
	}

	SetupButtons();

	// Get GLFW window from RE3
	GLFWwindow *window = glfwGetCurrentContext();
	if (!window) {
		fprintf(stderr, "TouchControls: No GLFW context\n");
		return;
	}

	// Create ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

	// Scale for mobile DPI
	float dpiScale = 3.0f;  // TODO: get from glfwGetWindowContentScale
	io.FontGlobalScale = dpiScale;
	ImGui::GetStyle().ScaleAllSizes(dpiScale);

	// Init backends — librw already loaded GL functions
	ImGui_ImplGlfw_InitForOpenGL(window, false);  // false = don't install callbacks
	ImGui_ImplOpenGL3_Init("#version 100");       // GLES2

	ms_imguiInitialized = true;
	fprintf(stderr, "TouchControls: ImGui initialized\n");
}

void
TouchControls::Shutdown(void)
{
	if (!ms_imguiInitialized) return;
	Reset();
	ms_numButtons = 0;

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	ms_imguiInitialized = false;
}

void
TouchControls::Reset(void)
{
	for (int i = 0; i < TOUCH_MAX_POINTS; i++) {
		ms_touches[i].active   = false;
		ms_touches[i].index    = -1;
		ms_touches[i].buttonIdx = -1;
		ms_touches[i].isStick  = false;
		ms_touches[i].isLook   = false;
		ms_touches[i].hasPrev  = false;
	}
	for (int i = 0; i < ms_numButtons; i++)
		ms_buttons[i].ClearState();

	ms_stickVisual.visible = false;
	ms_lookDeltaX = 0.0f;
	ms_lookDeltaY = 0.0f;

	ms_menuLMB = false;
	ms_menuLMBPending = false;
	ms_menuPressConsumed = false;
	ms_menuReleaseQueued = false;
	ms_menuCursorValid = false;

	ms_currentLayout = TOUCH_LAYOUT_NONE;
}

void
TouchControls::UpdatePhysicalScale(GLFWwindow *window)
{
	// Try to get window from current context if not provided
	if (window == nil)
		window = glfwGetCurrentContext();
	if (window == nil)
		return;

	// Get monitor
	GLFWmonitor *monitor = glfwGetWindowMonitor(window);
	if (monitor == nil)
		monitor = glfwGetPrimaryMonitor();
	if (monitor == nil)
		return;

	// Get physical monitor size in mm
	int physWidthMM, physHeightMM;
	glfwGetMonitorPhysicalSize(monitor, &physWidthMM, &physHeightMM);
	if (physWidthMM <= 0 || physHeightMM <= 0)
		return;

	// Get monitor resolution for DPI calculation
	const GLFWvidmode *videoMode = glfwGetVideoMode(monitor);
	if (videoMode == nil)
		return;
	if (videoMode->width <= 0 || videoMode->height <= 0)
		return;

	// Calculate actual DPI-based mm per monitor pixel
	float mmPerMonitorPixelX = (float)physWidthMM / (float)videoMode->width;
	float mmPerMonitorPixelY = (float)physHeightMM / (float)videoMode->height;

	// Get window and game dimensions
	int winW, winH;
	float gameW, gameH;

#ifdef OFFSCREEN_RENDER
	if (OffscreenRenderer::IsInitialized()) {
		winW = OffscreenRenderer::GetWindowWidth();
		winH = OffscreenRenderer::GetWindowHeight();
		gameW = (float)OffscreenRenderer::GetRenderWidth();
		gameH = (float)OffscreenRenderer::GetRenderHeight();
	} else
#endif
	{
		// Fallback to GLFW/RsGlobal
		glfwGetWindowSize(window, &winW, &winH);
		gameW = (float)RsGlobal.maximumWidth;
		gameH = (float)RsGlobal.maximumHeight;
	}

	if (winW <= 0 || winH <= 0)
		return;
	if (gameW <= 0.0f || gameH <= 0.0f)
		return;

	// Window pixels per game pixel (default - without rotations)
	float winPixPerGameX = (float)winW / gameW;
	float winPixPerGameY = (float)winH / gameH;

#ifdef OFFSCREEN_RENDER
	// With rotation, physical axes swap relative to game axes
	if (OffscreenRenderer::IsSideways()) {
		// Game X maps to physical Y axis, Game Y maps to physical X axis
		// Swap the mm-per-monitor-pixel values
		float temp = mmPerMonitorPixelX;
		mmPerMonitorPixelX = mmPerMonitorPixelY;
		mmPerMonitorPixelY = temp;

		// Also recalculate window-to-game ratio for rotated case
		winPixPerGameX = (float)winH / gameW;
		winPixPerGameY = (float)winW / gameH;
	}
#endif

	ms_mmPerGamePixelX = mmPerMonitorPixelX * winPixPerGameX;
	ms_mmPerGamePixelY = mmPerMonitorPixelY * winPixPerGameY;

	// Pixel aspect: ratio of screen scale factors
	// Used to correct circles on non-square pixel displays
	ms_pixelAspect = (SCREEN_SCALE_X(1.0f) > 0.0001f)
		? SCREEN_SCALE_Y(1.0f) / SCREEN_SCALE_X(1.0f)
		: 1.0f;
}

// ============================================================
// Touch slot helpers
// ============================================================

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

// ============================================================
// Hit testing
// ============================================================

int
TouchControls::HitTestButton(double x, double y)
{
	for (int i = 0; i < ms_numButtons; i++) {
		TouchButton &btn = ms_buttons[i];
		if (!(btn.layoutFlags & ms_currentLayout))
			continue;

		if (btn.roundVisual) {
			// Circle hit test
			float cx = btn.screenX + btn.screenW * 0.5f;
			float cy = btn.screenY + btn.screenH * 0.5f;
			float r  = btn.screenW * 0.5f;
			float dx = (float)x - cx;
			float dy = (float)y - cy;
			// Generous hit area — 120% of visual radius
			if (dx * dx + dy * dy <= r * r * 1.44f)
				return i;
		} else {
			// Rect hit test with small padding
			float pad = SCREEN_SCALE_X(8.0f);
			if (x >= btn.screenX - pad && x <= btn.screenX + btn.screenW + pad &&
			    y >= btn.screenY - pad && y <= btn.screenY + btn.screenH + pad)
				return i;
		}
	}
	return -1;
}

bool
TouchControls::IsLeftStickZone(double x, double y)
{
	return x < SCREEN_WIDTH * 0.5f;
}

bool
TouchControls::IsRightLookZone(double x, double y)
{
	return x >= SCREEN_WIDTH * 0.5f;
}

// ============================================================
// Touch handlers
// ============================================================

void
TouchControls::HandleTouchDown(int touchIndex, double x, double y)
{
	if (!ms_enabled) return;
	UpdateLayout();

	// --- Menu: handle as mouse ---
	if (ms_currentLayout == TOUCH_LAYOUT_MENU) {
		// Check buttons first (back button)
		int btnIdx = HitTestButton(x, y);
		if (btnIdx >= 0) {
			TouchButton &btn = ms_buttons[btnIdx];
			btn.pressed = true;
			btn.pending = true;
			btn.active = false;
			btn.consumed = false;
			btn.releaseQueued = false;
			btn.touchIndex = touchIndex;
			// Track in touch array so we can match release
			TouchPoint *tp = FindFreeTouchSlot();
			if (tp) {
				tp->active = true;
				tp->index = touchIndex;
				tp->x = x; tp->y = y;
				tp->buttonIdx = btnIdx;
				tp->isStick = false;
				tp->isLook = false;
			}
			return;
		}

		// Otherwise: menu tap → mouse LMB
		ms_menuCursorX = x;
		ms_menuCursorY = y;
		ms_menuCursorValid = true;
		ms_menuLMBPending = true;
		ms_menuLMB = false;
		ms_menuPressConsumed = false;
		ms_menuReleaseQueued = false;

		FrontEndMenuManager.m_nMouseTempPosX = (int32)x;
		FrontEndMenuManager.m_nMouseTempPosY = (int32)y;

		// Track this touch
		TouchPoint *tp = FindFreeTouchSlot();
		if (tp) {
			tp->active = true;
			tp->index = touchIndex;
			tp->x = x; tp->y = y;
			tp->buttonIdx = -1;
			tp->isStick = false;
			tp->isLook = false;
		}
		return;
	}

	// --- Cutscene: only buttons ---
	if (ms_currentLayout == TOUCH_LAYOUT_CUTSCENE) {
		int btnIdx = HitTestButton(x, y);
		if (btnIdx >= 0) {
			TouchButton &btn = ms_buttons[btnIdx];
			btn.pressed = true;
			btn.pending = true;
			btn.active = false;
			btn.consumed = false;
			btn.releaseQueued = false;
			btn.touchIndex = touchIndex;
			TouchPoint *tp = FindFreeTouchSlot();
			if (tp) {
				tp->active = true;
				tp->index = touchIndex;
				tp->x = x; tp->y = y;
				tp->buttonIdx = btnIdx;
				tp->isStick = false;
				tp->isLook = false;
			}
		}
		return;
	}

	// --- Gameplay ---

	// 1. Check buttons first
	int btnIdx = HitTestButton(x, y);
	if (btnIdx >= 0) {
		TouchButton &btn = ms_buttons[btnIdx];
		btn.pressed = true;
		btn.pending = true;
		btn.active = false;
		btn.consumed = false;
		btn.releaseQueued = false;
		btn.touchIndex = touchIndex;
		TouchPoint *tp = FindFreeTouchSlot();
		if (tp) {
			tp->active = true;
			tp->index = touchIndex;
			tp->x = x; tp->y = y;
			tp->prevX = x; tp->prevY = y;
			tp->hasPrev = false;
			tp->buttonIdx = btnIdx;
			tp->isStick = false;
			tp->isLook = btn.allowLook;
		}
		return;
	}

	// 2. Left stick zone (no active stick yet)
	bool hasStick = false;
	for (int i = 0; i < TOUCH_MAX_POINTS; i++)
		if (ms_touches[i].active && ms_touches[i].isStick) { hasStick = true; break; }

	if (!hasStick && IsLeftStickZone(x, y)) {
		TouchPoint *tp = FindFreeTouchSlot();
		if (tp) {
			tp->active = true;
			tp->index  = touchIndex;
			tp->x = tp->startX = x;
			tp->y = tp->startY = y;
			tp->prevX = x; tp->prevY = y;
			tp->hasPrev = false;
			tp->buttonIdx = -1;
			tp->isStick = true;
			tp->isLook = false;

			ms_stickVisual.visible = true;
			ms_stickVisual.baseX   = (float)x;
			ms_stickVisual.baseY   = (float)y;
			ms_stickVisual.thumbX  = (float)x;
			ms_stickVisual.thumbY  = (float)y;
			ms_stickVisual.radius  = ms_stickRadius;
		}
		return;
	}

	// 3. Right look zone
	bool hasLook = false;
	for (int i = 0; i < TOUCH_MAX_POINTS; i++)
		if (ms_touches[i].active && ms_touches[i].isLook) { hasLook = true; break; }

	if (!hasLook && IsRightLookZone(x, y)) {
		TouchPoint *tp = FindFreeTouchSlot();
		if (tp) {
			tp->active = true;
			tp->index  = touchIndex;
			tp->x = x; tp->y = y;
			tp->prevX = x; tp->prevY = y;
			tp->hasPrev = false;
			tp->buttonIdx = -1;
			tp->isStick = false;
			tp->isLook = true;
		}
		return;
	}
}

void
TouchControls::HandleTouchMove(int touchIndex, double x, double y)
{
	if (!ms_enabled) return;

	TouchPoint *tp = FindTouchByIndex(touchIndex);
	if (tp == nil) return;

	tp->prevX = tp->x;
	tp->prevY = tp->y;
	tp->x = x;
	tp->y = y;

	// Menu: update cursor
	if (ms_currentLayout == TOUCH_LAYOUT_MENU && tp->buttonIdx < 0) {
		ms_menuCursorX = x;
		ms_menuCursorY = y;
		ms_menuCursorValid = true;
		FrontEndMenuManager.m_nMouseTempPosX = (int32)x;
		FrontEndMenuManager.m_nMouseTempPosY = (int32)y;
		tp->hasPrev = true;
		return;
	}

	// Button touches: only allow look-through, no other movement
	if (tp->buttonIdx >= 0) {
		if (tp->isLook && tp->hasPrev) {
			ms_lookDeltaX += (float)(x - tp->prevX) * ms_mmPerGamePixelX * ms_lookSensitivity;
			ms_lookDeltaY += (float)(tp->prevY - y) * ms_mmPerGamePixelY * ms_lookSensitivity;
		}
		tp->hasPrev = true;
		return;
	}

	// Left stick
	if (tp->isStick) {
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

	// Right look
	if (tp->isLook && tp->hasPrev) {
		ms_lookDeltaX += (float)(x - tp->prevX) * ms_mmPerGamePixelX * ms_lookSensitivity;
		ms_lookDeltaY += (float)(tp->prevY - y) * ms_mmPerGamePixelY * ms_lookSensitivity;
	}

	tp->hasPrev = true;
}

void
TouchControls::HandleTouchUp(int touchIndex, double x, double y)
{
	if (!ms_enabled) return;

	TouchPoint *tp = FindTouchByIndex(touchIndex);
	if (tp == nil) return;

	// Button release
	if (tp->buttonIdx >= 0 && tp->buttonIdx < ms_numButtons) {
		TouchButton &btn = ms_buttons[tp->buttonIdx];
		btn.pressed = false;
		btn.releaseQueued = true;
		btn.touchIndex = -1;
	}

	// Menu mouse release
	if (ms_currentLayout == TOUCH_LAYOUT_MENU && tp->buttonIdx < 0) {
		ms_menuReleaseQueued = true;
	}

	// Stick release
	if (tp->isStick)
		ms_stickVisual.visible = false;

	tp->active    = false;
	tp->index     = -1;
	tp->buttonIdx = -1;
	tp->isStick   = false;
	tp->isLook    = false;
	tp->hasPrev   = false;
}

// ============================================================
// Per-frame apply: joystick
// ============================================================

void
TouchControls::ApplyToJoyState(void)
{
	if (!ms_enabled) return;
	if (ms_currentLayout != TOUCH_LAYOUT_GAMEPLAY) return;

	// Find the stick touch
	TouchPoint *stickTouch = nil;
	for (int i = 0; i < TOUCH_MAX_POINTS; i++) {
		if (ms_touches[i].active && ms_touches[i].isStick) {
			stickTouch = &ms_touches[i];
			break;
		}
	}
	if (stickTouch == nil) return;

	float dx = (float)(stickTouch->x - stickTouch->startX);
	float dy = (float)(stickTouch->y - stickTouch->startY);
	float dist = sqrtf(dx * dx + dy * dy);

	if (dist < ms_stickRadius * ms_stickDeadzone)
		return;

	float nx = dx / ms_stickRadius;
	float ny = dy / ms_stickRadius;
	if (nx > 1.0f) nx = 1.0f;
	if (nx < -1.0f) nx = -1.0f;
	if (ny > 1.0f) ny = 1.0f;
	if (ny < -1.0f) ny = -1.0f;

	float magnitude = sqrtf(nx * nx + ny * ny);
	if (magnitude > ms_stickDeadzone) {
		float remapped = (magnitude - ms_stickDeadzone) / (1.0f - ms_stickDeadzone);
		if (remapped > 1.0f) remapped = 1.0f;
		nx = nx / magnitude * remapped;
		ny = ny / magnitude * remapped;
	}

	CPad *pad = CPad::GetPad(0);
	pad->PCTempJoyState.LeftStickX = (int16)(nx * 128.0f);
	pad->PCTempJoyState.LeftStickY = (int16)(ny * 128.0f);
}

// ============================================================
// Per-frame apply: mouse
// ============================================================

void
TouchControls::ApplyToMouse(float &outDeltaX, float &outDeltaY, bool &outLMB, bool &outConsumed)
{
	outConsumed = false;
	outLMB = false;

	if (!ms_enabled) return;

	// --- Menu mouse latch ---
	if (ms_currentLayout == TOUCH_LAYOUT_MENU) {
		if (ms_menuCursorValid) {
			FrontEndMenuManager.m_nMouseTempPosX = (int32)ms_menuCursorX;
			FrontEndMenuManager.m_nMouseTempPosY = (int32)ms_menuCursorY;
			outConsumed = true;
		}

		// Frame 1: move cursor only
		if (ms_menuLMBPending) {
			ms_menuLMBPending = false;
			ms_menuLMB = true;
			return;
		}

		// Frame 2+: inject LMB
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

	// --- Gameplay look delta ---
	if (ms_lookDeltaX != 0.0f || ms_lookDeltaY != 0.0f) {
		outDeltaX = ms_lookDeltaX;
		outDeltaY = ms_lookDeltaY;
		outConsumed = true;
		ms_lookDeltaX = 0.0f;
		ms_lookDeltaY = 0.0f;
	}
}

// ============================================================
// Per-frame apply: buttons (1-frame latch pattern)
// ============================================================

void
TouchControls::ApplyButtons(void)
{
	if (!ms_enabled) return;
	UpdateLayout();

	for (int i = 0; i < ms_numButtons; i++) {
		TouchButton &btn = ms_buttons[i];
		if (!(btn.layoutFlags & ms_currentLayout))
			continue;

		// Latch step 1: pending → arm
		if (btn.pending) {
			btn.pending = false;
			btn.active = true;
			continue;  // don't inject this frame
		}

		// Latch step 2: active → inject
		if (btn.active) {
			switch (btn.actionType) {
			case TACTION_KEY:  InjectKey(btn.actionCode, true); break;
			case TACTION_PAD:  InjectPad(btn.actionCode, true); break;
			default: break;
			}
			btn.consumed = true;

			if (btn.releaseQueued) {
				btn.active = false;
				btn.releaseQueued = false;
			}
		}
	}
}

// ============================================================
// Injection helpers
// ============================================================

void
TouchControls::InjectKey(int32 keyID, bool pressed)
{
	int16 val = pressed ? 1 : 0;
	switch (keyID) {
	case TKEY_ESC:   CPad::NewKeyState.ESC = val; break;
	case TKEY_ENTER: CPad::NewKeyState.EXTENTER = val; break;
	case TKEY_TAB:   CPad::NewKeyState.TAB = val; break;
	case TKEY_SPACE: CPad::NewKeyState.VK_KEYS[' '] = val; break;
	}
}

void
TouchControls::InjectPad(int32 padBtn, bool pressed)
{
	int16 val = pressed ? 255 : 0;
	CPad *pad = CPad::GetPad(0);
	switch (padBtn) {
	case TPAD_CROSS:       pad->PCTempJoyState.Cross = val; break;
	case TPAD_SQUARE:      pad->PCTempJoyState.Square = val; break;
	case TPAD_CIRCLE:      pad->PCTempJoyState.Circle = val; break;
	case TPAD_TRIANGLE:    pad->PCTempJoyState.Triangle = val; break;
	case TPAD_L1:          pad->PCTempJoyState.LeftShoulder1 = val; break;
	case TPAD_R1:          pad->PCTempJoyState.RightShoulder1 = val; break;
	case TPAD_L2:          pad->PCTempJoyState.LeftShoulder2 = val; break;
	case TPAD_R2:          pad->PCTempJoyState.RightShoulder2 = val; break;
	case TPAD_DPAD_UP:     pad->PCTempJoyState.DPadUp = val; break;
	case TPAD_DPAD_DOWN:   pad->PCTempJoyState.DPadDown = val; break;
	case TPAD_DPAD_LEFT:   pad->PCTempJoyState.DPadLeft = val; break;
	case TPAD_DPAD_RIGHT:  pad->PCTempJoyState.DPadRight = val; break;
	case TPAD_START:       pad->PCTempJoyState.Start = val; break;
	case TPAD_SELECT:      pad->PCTempJoyState.Select = val; break;
	case TPAD_RIGHT_STICK: pad->PCTempJoyState.RightShock = val; break;
	case TPAD_LEFT_STICK:  pad->PCTempJoyState.LeftShock = val; break;
	}
}

// ============================================================
// Drawing
// ============================================================
void TouchControls::Draw(void)
{
	if (!ms_enabled) return;
	if (!ms_imguiInitialized) {
		Init();  // Lazy init
		if (!ms_imguiInitialized) return;
	}

	UpdateLayout();

	// Start ImGui frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame(OffscreenRenderer::GetRenderWidth(), OffscreenRenderer::GetRenderHeight());
	ImGui::NewFrame();

	ImDrawList *drawList = ImGui::GetBackgroundDrawList();

	// Draw buttons for current Layout
	for (int i = 0; i < ms_numButtons; i++) {
		TouchButton &btn = ms_buttons[i];
		if (!(btn.layoutFlags & ms_currentLayout)) continue;

		uint8 alpha = btn.pressed ? btn.pressedAlpha : btn.normalAlpha;
		ImU32 bgColor = IM_COL32(btn.bgR, btn.bgG, btn.bgB, alpha);
		ImU32 textColor = IM_COL32(255, 255, 255, alpha);

		ImVec2 pos(btn.screenX, btn.screenY);
		ImVec2 size(btn.screenW, btn.screenH);

		if (btn.roundVisual) {
			float cx = pos.x + size.x * 0.5f;
			float cy = pos.y + size.y * 0.5f;
			float r = size.x * 0.5f;
			drawList->AddCircleFilled(ImVec2(cx, cy), r, bgColor, 32);
		} else {
			drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bgColor, 8.0f);
		}

		// Label
		if (btn.label && btn.label[0]) {
			ImVec2 textSize = ImGui::CalcTextSize(btn.label);
			float tx = pos.x + (size.x - textSize.x) * 0.5f;
			float ty = pos.y + (size.y - textSize.y) * 0.5f;
			drawList->AddText(ImVec2(tx, ty), textColor, btn.label);
		}
	}

	// Draw stick (gameplay only)
	if (ms_currentLayout == TOUCH_LAYOUT_GAMEPLAY && ms_stickVisual.visible) {
		ImU32 baseCol = IM_COL32(255, 255, 255, (uint8)ms_stickBaseAlpha);
		ImU32 thumbCol = IM_COL32(255, 255, 255, (uint8)ms_stickThumbAlpha);
		
		drawList->AddCircleFilled(
			ImVec2(ms_stickVisual.baseX, ms_stickVisual.baseY),
			ms_stickVisual.radius, baseCol, 32);
		drawList->AddCircleFilled(
			ImVec2(ms_stickVisual.thumbX, ms_stickVisual.thumbY),
			25.0f, thumbCol, 32);
	}

	ImGui::Render();

	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

#endif // TOUCH_CONTROLS