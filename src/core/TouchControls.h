#pragma once

// On-screen touch controls for mobile/touch devices
// - Button system with layouts (menu / gameplay / cutscene)
// - Left stick (virtual analog)
// - Right-side look (camera)

#ifdef TOUCH_CONTROLS

#define TOUCH_MAX_POINTS 10
#define TOUCH_MAX_BUTTONS 24

// ---- Layout flags (bitmask) ----
enum eTouchLayout
{
	TOUCH_LAYOUT_NONE      = 0,
	TOUCH_LAYOUT_MENU      = 1,
	TOUCH_LAYOUT_GAMEPLAY  = 2,
	TOUCH_LAYOUT_CUTSCENE  = 4,
	TOUCH_LAYOUT_ALL       = 7,
};

// ---- Button action types ----
enum eTouchActionType
{
	TACTION_NONE = 0,
	TACTION_KEY,        // inject into CKeyboardState (ESC, ENTER, TAB, etc.)
	TACTION_PAD,        // inject into PCTempJoyState (Cross, Square, etc.)
	TACTION_MOUSE,      // inject mouse button (LMB, RMB)
};

// ---- Pad button IDs (for TACTION_PAD) ----
enum eTouchPadButton
{
	TPAD_CROSS,
	TPAD_SQUARE,
	TPAD_CIRCLE,
	TPAD_TRIANGLE,
	TPAD_L1,
	TPAD_R1,
	TPAD_L2,
	TPAD_R2,
	TPAD_DPAD_UP,
	TPAD_DPAD_DOWN,
	TPAD_DPAD_LEFT,
	TPAD_DPAD_RIGHT,
	TPAD_START,
	TPAD_SELECT,
	TPAD_LEFT_STICK,
	TPAD_RIGHT_STICK,
};

// ---- Key IDs (for TACTION_KEY) ----
enum eTouchKeyID
{
	TKEY_ESC,
	TKEY_ENTER,
	TKEY_TAB,
	TKEY_SPACE,
};

// ---- Mouse button IDs (for TACTION_MOUSE) ----
enum eTouchMouseBtn
{
	TMOUSE_LMB,
	TMOUSE_RMB,
};

// ---- Anchor for positioning ----
enum eTouchAnchor
{
	ANCHOR_TOP_LEFT,
	ANCHOR_TOP_RIGHT,
	ANCHOR_BOTTOM_LEFT,
	ANCHOR_BOTTOM_RIGHT,
	ANCHOR_TOP_CENTER,
	ANCHOR_BOTTOM_CENTER,
};

// ---- Touch Controls visibility mode
enum eTouchVisibility
{
	TVIS_ALWAYS = 0,        // always visible in layout
	TVIS_IN_VEHICLE,        // only in any vehicle
	TVIS_TAXI_MISSION,      // taxi/ambulance/police mission available
	TVIS_HAS_RADIO,         // vehicle has radio
};

// ---- Touch button definition ----
struct TouchButton
{
	// Identity
	const char *label;       // display text (short, 1-3 chars)

	// Layout
	uint32 layoutFlags;      // bitmask of eTouchLayout

	// Position — relative to anchor, in unscaled game units (will be SCREEN_SCALE'd)
	eTouchAnchor anchor;
	float offsetX, offsetY;
	float width, height;

	// Computed screen coordinates (filled each frame by UpdateLayout)
	float screenX, screenY, screenW, screenH;

	// Action
	eTouchActionType actionType;
	int32 actionCode;        // eTouchKeyID, eTouchPadButton, or eTouchMouseBtn

	// Visual
	uint8 bgR, bgG, bgB;
	uint8 normalAlpha;
	uint8 pressedAlpha;
	bool  roundVisual;       // draw as circle vs rectangle
	bool  allowLook;         // finger movement on this button also generates look delta

	// Runtime latch state (1-frame delay injection)
	bool  pressed;
	bool  pending;
	bool  active;
	bool  consumed;
	bool  releaseQueued;
	int   touchIndex;
	eTouchVisibility visibility;  // dynamic visibility condition

	void ClearState(void) {
		pressed = false;
		pending = false;
		active = false;
		consumed = false;
		releaseQueued = false;
		touchIndex = -1;
	}
};

struct GLFWwindow;
class TouchControls
{
public:
	struct TouchPoint
	{
		bool   active;
		int    index;
		double x, y;
		double startX, startY;
		double prevX, prevY;
		bool   hasPrev;
		int    buttonIdx;   // index into ms_buttons, or -1
		bool   isStick;
		bool   isLook;
	};

	struct StickVisual
	{
		bool   visible;
		float  baseX, baseY;   // center of the stick base (game coords)
		float  thumbX, thumbY; // current thumb position
		float  radius;         // max radius of movement
	};

	static void Init(void);
	static void Shutdown(void);

	// Called from touchCB - returns true if touch was consumed
	static void HandleTouchDown(int touchIndex, double x, double y);
	static void HandleTouchMove(int touchIndex, double x, double y);
	static void HandleTouchUp(int touchIndex, double x, double y);

	// Called each frame to apply accumulated input
	static void ApplyToJoyState(void);   // writes to PCTempJoyState.LeftStickX/Y
	static void ApplyToMouse(float &outDeltaX, float &outDeltaY, bool &outLMB, bool &outConsumed);
	static void ApplyButtons(void);

	// Draw the on-screen controls overlay
	static void Draw(void);

	// Reset all touches (e.g. on focus loss)
	static void Reset(void);

	// Call when window/monitor changes (from glfw.cpp)
	static void UpdatePhysicalScale(GLFWwindow *window);

	static bool IsActive(void) { return ms_enabled; }
	static void SetEnabled(bool enabled) { ms_enabled = enabled; }

	// Configuration
	static float ms_stickRadius;
	static float ms_stickDeadzone;
	static float ms_lookSensitivity;
	static float ms_stickBaseAlpha;
	static float ms_stickThumbAlpha;

private:
	static bool ms_enabled;
	static TouchPoint ms_touches[TOUCH_MAX_POINTS];
	static StickVisual ms_stickVisual;

	// Physical screen info for DPI-independent look
	static float ms_mmPerGamePixelX;  // millimeters per game pixel, X axis
	static float ms_mmPerGamePixelY;  // millimeters per game pixel, Y axis

	// Pixel aspect ratio for correct circle drawing
	static float ms_pixelAspect;      // SCREEN_SCALE_X(1) / SCREEN_SCALE_Y(1)

	// Look delta
	static float ms_lookDeltaX;
	static float ms_lookDeltaY;

	// Menu touch-to-mouse latching state
	static bool   ms_menuLMB;            // latched LMB state
	static bool   ms_menuLMBPending;     // press arrived, waiting 1 frame for cursor to settle
	static bool   ms_menuPressConsumed;  // true after at least one frame saw LMB=true
	static bool   ms_menuReleaseQueued;  // RELEASE arrived, waiting for press to be consumed
	static double ms_menuCursorX;        // cursor position to inject
	static double ms_menuCursorY;
	static bool   ms_menuCursorValid;

	// Buttons
	static TouchButton ms_buttons[TOUCH_MAX_BUTTONS];
	static int ms_numButtons;
	static uint32 ms_currentLayout;

	// Setup & layout
	static void SetupButtons(void);
	static bool IsButtonVisible(const TouchButton &btn);
	static void UpdateLayout(void);
	static uint32 DetectLayout(void);

	// Hit detection
	static int  HitTestButton(double x, double y);
	static bool IsLeftStickZone(double x, double y);
	static bool IsRightLookZone(double x, double y);

	// Touch helpers
	static TouchPoint* FindTouchByIndex(int touchIndex);
	static TouchPoint* FindFreeTouchSlot(void);

	// Injection helpers
	static void InjectKey(int32 keyID, bool pressed);
	static void InjectPad(int32 padBtn, bool pressed);

	// В private секцию класса TouchControls добавить:

	// ---- Cached geometry for optimized drawing ----
	static const int CIRCLE_SEGMENTS = 24;
	
	// Pre-calculated unit circle (computed once in Init)
	static float ms_unitCircleX[CIRCLE_SEGMENTS + 1];
	static float ms_unitCircleY[CIRCLE_SEGMENTS + 1];
	
	// Cached vertex buffers (avoid per-frame stack allocation)
	static RwIm2DVertex ms_circleVerts[CIRCLE_SEGMENTS + 2];
	
	// Cached Z values (updated once per frame in BeginDraw)
	static float ms_cachedNearZ;
	static float ms_cachedRecipZ;
	static bool  ms_renderStateSet;

	static bool ms_imguiInitialized;
};

#endif // TOUCH_CONTROLS