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

#include "../extras/custompipes.h"
#include "../extras/AuroraPerf.h"
#include "../extras/imgui/imgui.h"
#include "../extras/imgui/backends/imgui_impl_glfw.h"
#include "../extras/imgui/backends/imgui_impl_opengl3.h"

#include "Vehicle.h"
#include "Ped.h"
#include "Pad.h"
#include "World.h"
#include "PlayerPed.h"
#include "ModelIndices.h"
#include "Script.h"
#include "Pickups.h"
#include "PlayerInfo.h"

#include "Timer.h"
#ifdef EXTENDED_COLOURFILTER
#include "postfx.h"
#endif

#include <string>
#include <math.h>

static int activeTouch = -1; // For ImGui

int TouchControls::ms_activeTab = TouchControls::TAB_PREFS;
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
bool TouchControls::ms_showSettings = false;

TouchControls::DebugOverlaySettings TouchControls::ms_debugSettings = {
    false,   // enabled
    true,   // showFPS
    false,  // showBufferSize
    false   // showColorFilter
};

static std::string GetConfigPath()
{
	const char *home = getenv("HOME");
	if (home) {
		return std::string(home) + "/.config/ru.sashikknox/miami/settings.ini";
	}
	return "./settings.ini";
}
// Forward declarations for cheat functions (defined in Pad.cpp)
void WeaponCheat1();
void WeaponCheat2();
void WeaponCheat3();
void HealthCheat();
void ArmourCheat();
void MoneyCheat();
void WantedLevelUpCheat();
void WantedLevelDownCheat();
// void TankCheat();
void BlowUpCarsCheat();
void ChangePlayerCheat();
void MayhemCheat();
void EverybodyAttacksPlayerCheat();
void WeaponsForAllCheat();
void FastTimeCheat();
void SlowTimeCheat();
void SunnyWeatherCheat();
void CloudyWeatherCheat();
void RainyWeatherCheat();
void FoggyWeatherCheat();
void FastWeatherCheat();
void OnlyRenderWheelsCheat();
void ChittyChittyBangBangCheat();
void StrongGripCheat();
// void NastyLimbsCheat();
#ifdef KANGAROO_CHEAT
void KangarooCheat();
#endif

extern void SpawnCar(int id);
extern const char *carnames[];

static bool PlayerHasAimWeapon(void)
{
	CPlayerPed *player = FindPlayerPed();
	if (!player) return false;

	eWeaponType weapon = player->GetWeapon()->m_eWeaponType;

	// TODO: Fix me, need all aim weapons for vice city
	// First-person aim weapons in Vice City 
	return weapon == WEAPONTYPE_SNIPERRIFLE ||
	       weapon == WEAPONTYPE_ROCKETLAUNCHER;
}

// ============================================================
// Helper: add a button to the array
// ============================================================

static int AddButton(TouchButton *arr, int &count,
                     const char *label, uint32 layout,
                     eTouchAnchor anchor, float ox, float oy, float w, float h,
                     eTouchActionType atype, int32 acode,
                     uint8 r, uint8 g, uint8 b, uint8 normA, uint8 pressA,
                     bool round, bool allowLook = false,
                     eTouchVisibility vis = TVIS_ALWAYS)
{
	if (count >= TOUCH_MAX_BUTTONS) return -1;
	TouchButton &btn = arr[count];
	btn.visibility   = vis;
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

	// Settings button (gear icon) — top right, always visible
	AddButton(ms_buttons, ms_numButtons,
		"@",  // "⚙"
		TOUCH_LAYOUT_GAMEPLAY | TOUCH_LAYOUT_MENU,
		ANCHOR_TOP_RIGHT, 60.0f, 15.0f,
		40.0f, 40.0f,
		TACTION_SETTINGS, 0,  // новый action type
		80, 80, 80, 100, 200,
		false, false,
		TVIS_ALWAYS);

	// ---- Cut SCenes Layout
	AddButton(ms_buttons, ms_numButtons,
		"SKIP SCENE",
		TOUCH_LAYOUT_CUTSCENE,
		ANCHOR_TOP_CENTER, 0.0f, 15.0f,
		140.0f, 45.0f,
		TACTION_PAD, TPAD_CROSS,        // Triangle = back in menu
		40, 40, 40, 120, 200,
		false);

	// ---- MENU layout ----
	// Back button (Triangle) — top-left
	AddButton(ms_buttons, ms_numButtons,
		"<",
		TOUCH_LAYOUT_MENU,
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
		ANCHOR_BOTTOM_RIGHT, 90.0f, 160.0f,
		55.0f, 55.0f,
		TACTION_PAD, TPAD_CIRCLE,
		180, 50, 50, 100, 200,
		true, true,
		TVIS_ON_FOOT);

	// Aim toggle (sniper/RPG/M16) — binding-independent
	AddButton(ms_buttons, ms_numButtons,
		"AIM",                             
		TOUCH_LAYOUT_GAMEPLAY,
		ANCHOR_BOTTOM_RIGHT, 30.0f, 295.0f,
		55.0f, 55.0f,                      // size
		TACTION_AIM_TOGGLE, 0,             // action type, code unused
		200, 60, 60, 100, 220,             // red color (r,g,b, normal alpha, pressed alpha)
		true, true,                        // round, allow look-through
		TVIS_HAS_AIM_WEAPON);              // only visible with aim weapons

	// Enter-vehicle / Triangle (Y) — above the cluster
	AddButton(ms_buttons, ms_numButtons,
		"Y",
		TOUCH_LAYOUT_GAMEPLAY,
		ANCHOR_BOTTOM_RIGHT, 30.0f, 220.0f,
		55.0f, 55.0f,
		TACTION_PAD, TPAD_TRIANGLE,
		180, 180, 50, 100, 200,
		true);

	// Weapon cycle left (L2) — top center, left side
	AddButton(ms_buttons, ms_numButtons,
		"<<",
		TOUCH_LAYOUT_GAMEPLAY,
		ANCHOR_TOP_CENTER, -60.0f, 15.0f,
		50.0f, 35.0f,
		TACTION_PAD, TPAD_L2,
		60, 20, 20, 90, 180,
		false, false,
		TVIS_ON_FOOT);

	// Weapon cycle right (R2) — top center, right side
	AddButton(ms_buttons, ms_numButtons,
		">>",
		TOUCH_LAYOUT_GAMEPLAY,
		ANCHOR_TOP_CENTER, 60.0f, 15.0f,
		50.0f, 35.0f,
		TACTION_PAD, TPAD_R2,
		60, 20, 20, 90, 180,
		false, false,
		TVIS_ON_FOOT);

	// L1 — top-center, radio
	AddButton(ms_buttons, ms_numButtons,
		"RADIO",
		TOUCH_LAYOUT_GAMEPLAY,
		ANCHOR_TOP_CENTER, 0.0f, 15.0f,
		60.0f, 35.0f,
		TACTION_PAD, TPAD_L1,
		80, 80, 80, 90, 180,
		false, false,
		TVIS_HAS_RADIO);

	AddButton(ms_buttons, ms_numButtons, // and it look back in on foot 
		"Mission",                        // taxi/ambulance/police etc mission icon 
		TOUCH_LAYOUT_GAMEPLAY,
		ANCHOR_BOTTOM_CENTER, 0.0f, 30.0f,
		65.0f, 35.0f,
		TACTION_PAD, TPAD_RIGHT_STICK,
		60, 180, 60, 120, 200,
		false, false,
		TVIS_TAXI_MISSION);

	// Buy property — visible only near a for-sale property pickup
	AddButton(ms_buttons, ms_numButtons,
		"BUY",
		TOUCH_LAYOUT_GAMEPLAY,
		ANCHOR_BOTTOM_CENTER, 0.0f, 30.0f,
		65.0f, 35.0f,
		TACTION_PAD, TPAD_L1,
		200, 180, 40, 140, 220,
		false, false,
		TVIS_NEAR_PROPERTY);

	// Horn — only in vehicle
	AddButton(ms_buttons, ms_numButtons,
		"!",
		TOUCH_LAYOUT_GAMEPLAY,
		ANCHOR_BOTTOM_RIGHT, 30.0f, 285.0f,
		50.0f, 50.0f,
		TACTION_PAD, TPAD_LEFT_STICK,
		20, 20, 80, 100, 200,
		true, false,
		TVIS_IN_VEHICLE);

	AddButton(ms_buttons, ms_numButtons,
		"B",
		TOUCH_LAYOUT_GAMEPLAY,
		ANCHOR_BOTTOM_RIGHT, 90.0f, 160.0f,
		55.0f, 55.0f,
		TACTION_PAD, TPAD_CIRCLE,
		180, 50, 50, 100, 200,
		true, true,
		TVIS_IN_VEHICLE);
}

bool
TouchControls::IsButtonVisible(const TouchButton &btn)
{
	// Layout check first
	if (!(btn.layoutFlags & ms_currentLayout))
		return false;

	// Dynamic visibility
	switch (btn.visibility) {
	case TVIS_ALWAYS:
		return true;

	case TVIS_IN_VEHICLE:
		return FindPlayerVehicle() != nil;

	case TVIS_HAS_RADIO:
		{
			CVehicle *veh = FindPlayerVehicle();
			return veh != nil && !veh->IsBoat();  // boats have no radio in GTA3
		}

	case TVIS_TAXI_MISSION:
		{
			CVehicle *veh = FindPlayerVehicle();
			if (veh == nil) return false;
			
			// Check vehicle type and mission not already active
			int32 model = veh->GetModelIndex();
			
			// Taxi: TAXI, CABBIE, BORGNINE
			// TODO: FIXME need all cars with missions
			if (model == MI_TAXI || model == MI_CABBIE /*|| model == MI_BORGNINE*/)
				return !CTheScripts::IsPlayerOnAMission();
			
			// Ambulance
			if (model == MI_AMBULAN)
				return !CTheScripts::IsPlayerOnAMission();
			
			// Police car (Vigilante)
			if (model == MI_POLICE || model == MI_ENFORCER || 
				model == MI_FBICAR || model == MI_RHINO)
				return !CTheScripts::IsPlayerOnAMission();
			
			// Firetruck
			if (model == MI_FIRETRUCK)
				return !CTheScripts::IsPlayerOnAMission();
			
			return false;
		}
	case TVIS_ON_FOOT:
		return FindPlayerVehicle() == nil;
	case TVIS_HAS_AIM_WEAPON:
		if (!PlayerHasAimWeapon())
			return false;
		break;
	case TVIS_NEAR_PROPERTY:
		{
			if (FindPlayerVehicle() != nil) return false;
			if (CTheScripts::IsPlayerOnAMission()) return false;
			CVector playerPos = FindPlayerCoors();
			for (int i = 0; i < NUMPICKUPS; i++) {
				CPickup &pk = CPickups::aPickUps[i];
				if (pk.m_eType != PICKUP_PROPERTY_FORSALE) continue;
				if (pk.m_bRemoved) continue;
				float dx = pk.m_vecPos.x - playerPos.x;
				float dy = pk.m_vecPos.y - playerPos.y;
				float dz = pk.m_vecPos.z - playerPos.z;
				if (dx*dx + dy*dy + dz*dz < 6.25f) // 2.5f radius
					return true;
			}
			return false;
		}
	}
	return true;
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

		if (!IsButtonVisible(btn))
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
	LoadSettings();

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
	// Clear aim toggle
	CPad::bTouchAimToggle = false;

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

void TouchControls::DrawDebugOverlay(void)
{
	if (!ms_debugSettings.enabled)
		return;

	// FPS calculation
	static float fps = 0.0f;
	static float fpsTimer = 0.0f;
	static int frameCount = 0;

	frameCount++;
	fpsTimer += CTimer::GetTimeStepNonClippedInSeconds();

	if (fpsTimer >= 0.5f) {
		fps = frameCount / fpsTimer;
		frameCount = 0;
		fpsTimer = 0.0f;
	}

	// Window setup
	ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.6f);
	ImGui::Begin("##Debug", nullptr, 
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav |
		ImGuiWindowFlags_NoInputs);

	// FPS with color coding
	ImVec4 fpsColor;
	if (fps >= 30.0f)
		fpsColor = ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
	else if (fps >= 20.0f)
		fpsColor = ImVec4(1.0f, 1.0f, 0.2f, 1.0f);
	else
		fpsColor = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
	if (ms_debugSettings.showFPS)
		ImGui::TextColored(fpsColor, "FPS: %.1f", fps);

#ifdef EXTENDED_COLOURFILTER
	const char *fxName = "?";
	switch(CPostFX::EffectSwitch) {
		case CPostFX::POSTFX_OFF:    fxName = "OFF";    break;
		case CPostFX::POSTFX_SIMPLE: fxName = "Simple"; break;
		case CPostFX::POSTFX_NORMAL: fxName = "Normal"; break;
		case CPostFX::POSTFX_MOBILE: fxName = "Mobile"; break;
	}
	if (ms_debugSettings.showColorFilter)
		ImGui::Text("FX: %s", fxName);
#endif

#ifdef OFFSCREEN_RENDER
	if (ms_debugSettings.showBufferSize)
		ImGui::Text("3D: %dx%d (%.0f%%)", 
			OffscreenRenderer::Get3DWidth(),
			OffscreenRenderer::Get3DHeight(),
			OffscreenRenderer::Get3DResolution() * 100.0f);
#endif

	ImGui::End();
}

void TouchControls::DrawSettingsPanel(void)
{
	if (!ms_showSettings)
		return;

	float xscale = 1.0f, yscale = 1.0f;
	GLFWwindow *win = glfwGetCurrentContext();
	if (win)
		glfwGetWindowContentScale(win, &xscale, &yscale);
	// contentScale=1.0 at 96 DPI (desktop default), ~2-3 on mobile HiDPI
	// clamp to sane range for UI
	const float uiScale = fmaxf(1.0f, fminf(yscale, 4.0f));

	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f * uiScale, 4.0f * uiScale));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * uiScale, 6.0f * uiScale));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * uiScale, 12.0f * uiScale));
	ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 21.0f * uiScale);
	ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 18.0f * uiScale);

	// ---- Main full-screen window (background for everything) ----
	ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(SCREEN_WIDTH, SCREEN_HEIGHT), ImGuiCond_Always);

	if (ImGui::Begin("Settings", &ms_showSettings,
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoScrollWithMouse))
	{
		ImGui::SetWindowFontScale(uiScale);

		// ---- Tab buttons (left column) ----
		const float tabW = 120.0f * uiScale;
		const float tabH = 45.0f * uiScale;
		const float tabGap = 6.0f * uiScale;
		const float contentPad = 8.0f * uiScale;
		const float contentX = tabW + contentPad * 2.0f;

		static const char *tabLabels[TAB_COUNT] = { "Pref", "Dbg", "Cheat" };

		ImGui::SetCursorPos(ImVec2(contentPad, contentPad));
		ImGui::BeginGroup();
		for (int i = 0; i < TAB_COUNT; i++) {
			bool active = (ms_activeTab == i);
			if (active) {
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 0.9f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.5f, 0.8f, 0.9f));
			} else {
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.8f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.8f));
			}
			if (ImGui::Button(tabLabels[i], ImVec2(tabW, tabH)))
				ms_activeTab = i;
			ImGui::PopStyleColor(2);
		}

		// Close button below tabs
		ImGui::Spacing();
		ImGui::Spacing();
		if (ImGui::Button("Close", ImVec2(tabW, tabH)))
			ms_showSettings = false;

		ImGui::EndGroup();

		// ---- Content area (right of tabs, scrollable child) ----
		float contentW = SCREEN_WIDTH - contentX - contentPad;
		float contentH = SCREEN_HEIGHT - contentPad * 2.0f;

		ImGui::SetCursorPos(ImVec2(contentX, contentPad));
		ImGui::BeginChild("##Content", ImVec2(contentW, contentH), true);
		ImGui::SetWindowFontScale(uiScale);

		static bool  scrollActive = false;  // past threshold
		// ---- Touch scroll with drag threshold + inertia ----
		{
			static float scrollVelocity = 0.0f;
			static float lastTouchY = 0.0f;
			static float startTouchY = 0.0f;
			static bool  dragging = false;
			const float  dragThreshold = 10.0f; // pixels before scroll starts
			const float  friction = 0.92f;
			const float  minVel = 0.5f;

			ImGuiIO &io = ImGui::GetIO();
			bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

			if (hovered && io.MouseDown[0]) {
				if (!dragging) {
					// Touch just started
					dragging = true;
					scrollActive = false;
					startTouchY = io.MousePos.y;
					lastTouchY = io.MousePos.y;
					scrollVelocity = 0.0f;
				} else {
					float totalDY = startTouchY - io.MousePos.y;
					if (!scrollActive && fabsf(totalDY) > dragThreshold) {
						// Crossed threshold — start scrolling, steal input
						scrollActive = true;
						lastTouchY = io.MousePos.y;
					}
					if (scrollActive) {
						float dy = lastTouchY - io.MousePos.y;
						scrollVelocity = dy;
						ImGui::SetScrollY(ImGui::GetScrollY() + dy);
						lastTouchY = io.MousePos.y;


					}
				}
			} else {
				if (dragging) {
					dragging = false;
					scrollActive = false;
				}
				// Inertia
				if (fabsf(scrollVelocity) > minVel) {
					ImGui::SetScrollY(ImGui::GetScrollY() + scrollVelocity);
					scrollVelocity *= friction;
				} else {
					scrollVelocity = 0.0f;
				}
			}
		}

		bool blockInput = scrollActive;
		if (blockInput) ImGui::BeginDisabled();

		// ========== TAB: Preferences ==========
		if (ms_activeTab == TAB_PREFS) {
			ImGui::Text("Graphics");
			ImGui::Separator();

			float scale = OffscreenRenderer::Get3DResolution();
			ImGui::Text("3D Resolution: %.0f%%", scale * 100.0f);
			if (ImGui::SliderFloat("##3DScale", &scale, 0.15f, 1.0f, "%.2f"))
				OffscreenRenderer::Set3DResolution(scale);

			if (ImGui::Button("15%")) OffscreenRenderer::Set3DResolution(0.15f);
			ImGui::SameLine();
			if (ImGui::Button("25%")) OffscreenRenderer::Set3DResolution(0.25f);
			ImGui::SameLine();
			if (ImGui::Button("50%")) OffscreenRenderer::Set3DResolution(0.5f);
			ImGui::SameLine();
			if (ImGui::Button("75%")) OffscreenRenderer::Set3DResolution(0.75f);
			ImGui::SameLine();
			if (ImGui::Button("100%")) OffscreenRenderer::Set3DResolution(1.0f);

			CAuroraPerf::RenderImGuiPanel();

			ImGui::Spacing();
			ImGui::Separator();
			if (ImGui::Button("Save Settings")) SaveSettings();
		}

		// ========== TAB: Debug ==========
		else if (ms_activeTab == TAB_DEBUG) {
			ImGui::Text("Debug Overlay");
			ImGui::Separator();

			ImGui::Checkbox("Enable Overlay", &ms_debugSettings.enabled);
			if (ms_debugSettings.enabled) {
				ImGui::Indent();
				ImGui::Checkbox("Show FPS", &ms_debugSettings.showFPS);
				ImGui::Checkbox("Show Buffer Size", &ms_debugSettings.showBufferSize);
				ImGui::Checkbox("Show Color Filter", &ms_debugSettings.showColorFilter);
				ImGui::Unindent();
			}

			ImGui::Spacing();
			ImGui::Separator();
			if (ImGui::Button("Save Settings")) SaveSettings();
		}

		// ========== TAB: Cheats ==========
		else if (ms_activeTab == TAB_CHEATS) {
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Warning: Using cheats may affect saves!");
			ImGui::Spacing();

			ImGui::Text("Player:");
			ImGui::Indent();
			if (ImGui::Button("Full Health##cheat")) HealthCheat();
			ImGui::SameLine();
			if (ImGui::Button("Full Armor##cheat")) ArmourCheat();
			if (ImGui::Button("$250,000##cheat")) MoneyCheat();
			ImGui::SameLine();
			if (ImGui::Button("Weapons 1##cheat")) WeaponCheat1();
			if (ImGui::Button("Weapons 2##cheat")) WeaponCheat2();
			if (ImGui::Button("Weapons 3##cheat")) WeaponCheat3();
			if (ImGui::Button("Change Player##cheat")) ChangePlayerCheat();
			ImGui::Unindent();

			ImGui::Spacing();
			ImGui::Text("Wanted Level:");
			ImGui::Indent();
			if (ImGui::Button("+ Star##cheat")) WantedLevelUpCheat();
			ImGui::SameLine();
			if (ImGui::Button("- Star##cheat")) WantedLevelDownCheat();
			ImGui::Unindent();

			ImGui::Spacing();
			ImGui::Text("Vehicles:");
			ImGui::Indent();
			// TODO: FIXME add more vehicles buttons (separate menu?)
			// if (ImGui::Button("Spawn Tank##cheat")) TankCheat();
			if (ImGui::Button("Blow Up Cars##cheat")) BlowUpCarsCheat();
			if (ImGui::Button("Flying Cars##cheat")) ChittyChittyBangBangCheat();
			ImGui::SameLine();
			if (ImGui::Button("Better Handling##cheat")) StrongGripCheat();
			if (ImGui::Button("Invisible Cars##cheat")) OnlyRenderWheelsCheat();

			ImGui::Spacing();
			ImGui::Text("Spawn Vehicle:");
			{
				extern const char *carnames[];
				static int spawnIdx = 0;
				int numCars = MI_LAST_VEHICLE - MI_FIRST_VEHICLE + 1;

				ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 28.0f * uiScale);
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f * uiScale, 10.0f * uiScale));
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * uiScale, 8.0f * uiScale));
				if (ImGui::BeginCombo("##SpawnVehicle", carnames[spawnIdx], ImGuiComboFlags_HeightLargest)) {
					ImGui::SetWindowFontScale(uiScale);
					for (int i = 0; i < numCars; i++) {
						bool selected = (spawnIdx == i);
						if (ImGui::Selectable(carnames[i], selected))
							spawnIdx = i;
						if (selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				ImGui::PopStyleVar(3);

				if (ImGui::Button("Spawn##vehicle")) {
					int id = MI_FIRST_VEHICLE + spawnIdx;
					if (id != MI_CHOPPER && id != MI_AIRTRAIN && id != MI_DEADDODO)
						SpawnCar(id);
				}
			}
			ImGui::Unindent();

			ImGui::Spacing();
			ImGui::Text("World:");
			ImGui::Indent();
			if (ImGui::Button("Mayhem##cheat")) MayhemCheat();
			ImGui::SameLine();
			if (ImGui::Button("Peds Attack##cheat")) EverybodyAttacksPlayerCheat();
			if (ImGui::Button("Peds Have Weapons##cheat")) WeaponsForAllCheat();
			ImGui::Unindent();

			ImGui::Spacing();
			ImGui::Text("Game Speed:");
			ImGui::Indent();
			if (ImGui::Button("Fast Time##cheat")) FastTimeCheat();
			ImGui::SameLine();
			if (ImGui::Button("Slow Time##cheat")) SlowTimeCheat();
			ImGui::Unindent();

			ImGui::Spacing();
			ImGui::Text("Weather:");
			ImGui::Indent();
			if (ImGui::Button("Sunny##cheat")) SunnyWeatherCheat();
			ImGui::SameLine();
			if (ImGui::Button("Cloudy##cheat")) CloudyWeatherCheat();
			ImGui::SameLine();
			if (ImGui::Button("Rainy##cheat")) RainyWeatherCheat();
			if (ImGui::Button("Foggy##cheat")) FoggyWeatherCheat();
			ImGui::SameLine();
			if (ImGui::Button("Crazy Weather##cheat")) FastWeatherCheat();
			ImGui::Unindent();

#ifdef KANGAROO_CHEAT
			ImGui::Spacing();
			ImGui::Text("Special:");
			ImGui::Indent();
			if (ImGui::Button("Kangaroo Jump##cheat")) KangarooCheat();
			ImGui::Unindent();
#endif
		}

		if (blockInput) ImGui::EndDisabled();

		ImGui::EndChild();
	}
	ImGui::End();

	ImGui::PopStyleVar(5);
}

void TouchControls::SaveSettings(void)
{
	FILE *f = fopen(GetConfigPath().c_str(), "w");
	if (!f) return;

	fprintf(f, "render3DScale=%f\n", OffscreenRenderer::Get3DResolution());
#ifdef EXTENDED_PIPELINES
	fprintf(f, "envMapEnabled=%d\n", CustomPipes::EnvMapEnabled ? 1 : 0);
#endif
	fprintf(f, "debugOverlay=%d\n", ms_debugSettings.enabled ? 1 : 0);
	fprintf(f, "showFPS=%d\n", ms_debugSettings.showFPS ? 1 : 0);
	fprintf(f, "showBufferSize=%d\n", ms_debugSettings.showBufferSize ? 1 : 0);
	fprintf(f, "showColorFilter=%d\n", ms_debugSettings.showColorFilter ? 1 : 0);
	
	CAuroraPerf::SaveToIni(f);

	fclose(f);
}

void TouchControls::LoadSettings(void)
{

	FILE *f = fopen(GetConfigPath().c_str(), "r");
	if (!f) return;

	char line[128];
	while (fgets(line, sizeof(line), f)) {
		float fval;
		int ival;

		if (line[0] == ';' || line[0] == '#' || line[0] == '\n' || line[0] == '[')
			continue;

		if (sscanf(line, "render3DScale=%f", &fval) == 1)
			OffscreenRenderer::Set3DResolution(fval);
#ifdef EXTENDED_PIPELINES
		else if (sscanf(line, "envMapEnabled=%d", &ival) == 1)
			CustomPipes::EnvMapEnabled = ival != 0;
#endif
		else if (sscanf(line, "debugOverlay=%d", &ival) == 1)
			ms_debugSettings.enabled = ival != 0;
		else if (sscanf(line, "showFPS=%d", &ival) == 1)
			ms_debugSettings.showFPS = ival != 0;
		else if (sscanf(line, "showBufferSize=%d", &ival) == 1)
			ms_debugSettings.showBufferSize = ival != 0;
		else if (sscanf(line, "showColorFilter=%d", &ival) == 1)
			ms_debugSettings.showColorFilter = ival != 0;
		else 
			CAuroraPerf::LoadFromIni(line);
	}

	fclose(f);
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

		if (!IsButtonVisible(btn))
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
	// First touch becomes active
	if (activeTouch == -1) {
		ImGuiIO &io = ImGui::GetIO();
		activeTouch = touchIndex;
		io.AddMousePosEvent((float)x, (float)y);
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
	}

	if (!ms_enabled || ms_showSettings)
		return;

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
	// Move/drag - only track active touch
	if (touchIndex == activeTouch) {
		ImGuiIO &io = ImGui::GetIO();
		io.AddMousePosEvent((float)x, (float)y);
	}

	if (!ms_enabled || ms_showSettings) 
		return;

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
	// Only handle release of active touch
	if (touchIndex == activeTouch) {
		ImGuiIO &io = ImGui::GetIO();
		activeTouch = -1;
		io.AddMousePosEvent((float)x, (float)y);
		io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
	}

	if (!ms_enabled || ms_showSettings) 
		return;

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

		// Handle aim toggle on press (not hold)
		if (btn.actionType == TACTION_AIM_TOGGLE && btn.pending) {
			btn.pending = false;
			btn.active = true;
			
			// Toggle the global aim state in CPad
			CPad::bTouchAimToggle = !CPad::bTouchAimToggle;
			if (CPad::bTouchAimToggle)
				CPad::bTouchAimJustPressed = true;  // fire "just down" once
			continue;
		}

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
			case TACTION_SETTINGS:
				ms_showSettings = !ms_showSettings;
				break;
			case TACTION_AIM_TOGGLE:// allready handled
			default: break;
			}
			btn.consumed = true;

			if (btn.releaseQueued) {
				btn.active = false;
				btn.releaseQueued = false;
			}
		}
	}

	// Auto-reset aim toggle if weapon changed or no longer has aim weapon
	static eWeaponType s_lastWeapon = WEAPONTYPE_UNARMED;
	CPlayerPed *player = FindPlayerPed();
	if (player) {
		eWeaponType curWeapon = player->GetWeapon()->m_eWeaponType;
		if (curWeapon != s_lastWeapon) {
			// Weapon changed — reset aim toggle
			CPad::bTouchAimToggle = false;
			s_lastWeapon = curWeapon;
		}
	}

	// Also reset if player is in vehicle
	if (FindPlayerVehicle()) {
		CPad::bTouchAimToggle = false;
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
	ImGui_ImplGlfw_NewFrame(OffscreenRenderer::GetRenderWidth(), OffscreenRenderer::GetRenderHeight(), activeTouch != -1);
	ImGui::NewFrame();

	DrawDebugOverlay();
	DrawSettingsPanel();

	ImDrawList *drawList = ImGui::GetBackgroundDrawList();

	// Draw buttons for current Layout
	for (int i = 0; i < ms_numButtons; i++) {
		TouchButton &btn = ms_buttons[i];

		if (!IsButtonVisible(btn))
			continue;

		uint8 alpha;
		if (btn.actionType == TACTION_AIM_TOGGLE && CPad::bTouchAimToggle) {
			// Aim toggle is ON — use bright/highlighted state
			alpha = 240;  // very visible
		} else {
			alpha = btn.pressed ? btn.pressedAlpha : btn.normalAlpha;
		}

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