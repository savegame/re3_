#pragma once

// On-screen touch controls for mobile/touch devices
// Left side: virtual analog stick (maps to gamepad left stick)
// Right side: look control (maps to mouse delta for camera)
// In menu: tap = mouse click at touch position

#ifdef TOUCH_CONTROLS

#define TOUCH_MAX_POINTS 10

class TouchControls
{
public:
	enum eTouchZone
	{
		ZONE_NONE = 0,
		ZONE_LEFT_STICK,
		ZONE_RIGHT_LOOK,
	};

	struct TouchPoint
	{
		bool   active;
		int    index;       // GLFW touch index
		double x, y;        // current position (game coords)
		double startX, startY; // where the finger first touched
		double prevX, prevY;   // previous frame position
		bool   hasPrev;
		eTouchZone zone;
	};

	// Joystick visual state
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

	// Draw the on-screen controls overlay
	static void Draw(void);

	// Reset all touches (e.g. on focus loss)
	static void Reset(void);

	static bool IsActive(void) { return ms_enabled; }
	static void SetEnabled(bool enabled) { ms_enabled = enabled; }

	// Configuration
	static float ms_stickRadius;      // max stick travel in game pixels
	static float ms_stickDeadzone;    // deadzone fraction (0..1)
	static float ms_lookSensitivity;  // multiplier for look deltas
	static float ms_stickBaseAlpha;   // visual alpha for stick base (0..255)
	static float ms_stickThumbAlpha;  // visual alpha for stick thumb

private:
	static bool ms_enabled;
	static bool ms_initialized;
	static TouchPoint ms_touches[TOUCH_MAX_POINTS];
	static StickVisual ms_stickVisual;

	// Accumulated look delta for this frame
	static float ms_lookDeltaX;
	static float ms_lookDeltaY;

	// Menu touch-to-mouse latching state
	static bool   ms_menuLMB;            // latched LMB state
	static bool   ms_menuLMBPending;     // press arrived, waiting 1 frame for cursor to settle
	static bool   ms_menuPressConsumed;  // true after at least one frame saw LMB=true
	static bool   ms_menuReleaseQueued;  // RELEASE arrived, waiting for press to be consumed
	static double ms_menuCursorX;        // cursor position to inject
	static double ms_menuCursorY;
	static bool   ms_menuCursorValid;    // have a position to inject

	static eTouchZone ClassifyZone(double x, double y);
	static TouchPoint* FindTouchByIndex(int touchIndex);
	static TouchPoint* FindFreeTouchSlot(void);
	static TouchPoint* FindTouchByZone(eTouchZone zone);

	static void DrawCircle(float cx, float cy, float radius, int segments, 
	                        uint8 r, uint8 g, uint8 b, uint8 a);
	static void DrawFilledCircle(float cx, float cy, float radius, int segments,
	                              uint8 r, uint8 g, uint8 b, uint8 a);
};

#endif // TOUCH_CONTROLS
