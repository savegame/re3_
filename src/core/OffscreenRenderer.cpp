/*
 * OffscreenRenderer.cpp - Offscreen rendering implementation
 * 
 * Supports dual-buffer rendering:
 *   - 3D camera renders at configurable resolution
 *   - UI buffer renders at native resolution
 *   - Final blit to screen with rotation
 */

#ifdef OFFSCREEN_RENDER

#include "common.h"
#include "main.h"
#include "Camera.h"
#include "Draw.h"
#include "RwHelper.h"
#include "OffscreenRenderer.h"
#include <GLFW/glfw3.h>


#include "../rw/VisibilityPlugins.h"
#include "Clouds.h"
#include "Sprite.h"
#include "Timecycle.h"
#include "Weather.h"
#include "ZoneCull.h"
#include "TouchControls.h"
// =============================================================================
// Static member initialization
// =============================================================================

// UI buffer
RwRaster *OffscreenRenderer::ms_offscreenColor = nil;
RwRaster *OffscreenRenderer::ms_offscreenDepth = nil;
RwRaster *OffscreenRenderer::ms_origColor = nil;
RwRaster *OffscreenRenderer::ms_origDepth = nil;

// 3D camera
RwCamera *OffscreenRenderer::ms_3dCamera = nil;
RwFrame *OffscreenRenderer::ms_3dFrame = nil;
RwCamera *OffscreenRenderer::ms_savedSceneCamera = nil;

// Cached blit geometry
RwIm2DVertex OffscreenRenderer::ms_3dBlitVerts[4];
RwImVertexIndex OffscreenRenderer::ms_3dBlitIndices[6] = { 0, 1, 2, 0, 2, 3 };
RwIm2DVertex OffscreenRenderer::ms_screenBlitVerts[4];
RwImVertexIndex OffscreenRenderer::ms_screenBlitIndices[6] = { 0, 1, 2, 0, 2, 3 };

// State
bool OffscreenRenderer::ms_initialized = false;
bool OffscreenRenderer::ms_enabled = true;
bool OffscreenRenderer::ms_inFrame = false;
bool OffscreenRenderer::ms_in3D = false;

int OffscreenRenderer::ms_savedRsWidth = 0;
int OffscreenRenderer::ms_savedRsHeight = 0;

// UI parameters
int OffscreenRenderer::ms_renderWidth = 0;
int OffscreenRenderer::ms_renderHeight = 0;
int OffscreenRenderer::ms_windowWidth = 0;
int OffscreenRenderer::ms_windowHeight = 0;
float OffscreenRenderer::ms_renderAspect = 16.0f/9.0f;
OffscreenRenderer::Rotation OffscreenRenderer::ms_rotation = ROTATE_0;

// 3D parameters
int OffscreenRenderer::ms_3dWidth = 0;
int OffscreenRenderer::ms_3dHeight = 0;
float OffscreenRenderer::ms_3dScale = 0.5f;  // Default: half resolution

// =============================================================================
// Initialization / Shutdown
// =============================================================================

bool
OffscreenRenderer::Init()
{
    if (ms_initialized) {
        debug("OffscreenRenderer already initialized\n");
        return true;
    }
    
    if (Scene.camera == nil) {
        debug("OffscreenRenderer::Init - Scene.camera is null!\n");
        return false;
    }
    
    // Get actual window size from GLFW
    GLFWwindow* window = glfwGetCurrentContext();
    if (window == nil) {
        debug("OffscreenRenderer::Init - No GLFW context!\n");
        return false;
    }
    
    int winW, winH;
    glfwGetFramebufferSize(window, &winW, &winH);
    
    ms_windowWidth = winW;
    ms_windowHeight = winH;
    
    // Determine orientation and set render dimensions
    if (winW < winH) {
        // Portrait window -> render in landscape with 90 degree rotation
        ms_rotation = ROTATE_90;
        ms_renderWidth = winH;
        ms_renderHeight = winW;
        debug("Portrait window %dx%d -> landscape render %dx%d (rotation 90)\n",
              winW, winH, ms_renderWidth, ms_renderHeight);
    } else {
        // Landscape window -> no rotation needed
        ms_rotation = ROTATE_0;
        ms_renderWidth = winW;
        ms_renderHeight = winH;
        debug("Landscape window %dx%d -> no rotation\n", winW, winH);
    }
    
    // Set Wayland transform
    int wl_transform = GLFW_TRANSFORM_NORMAL;
    switch (ms_rotation) {
    case ROTATE_0:   wl_transform = GLFW_TRANSFORM_NORMAL; break;
    case ROTATE_90:  wl_transform = GLFW_TRANSFORM_270;    break;
    case ROTATE_180: wl_transform = GLFW_TRANSFORM_180;    break;
    case ROTATE_270: wl_transform = GLFW_TRANSFORM_90;     break;
    }
    glfwSetWindowContentTransform(window, wl_transform);
    
    ms_renderAspect = (float)ms_renderWidth / (float)ms_renderHeight;

    // Create UI FBO
    if (!CreateOffscreenBuffers(ms_renderWidth, ms_renderHeight)) {
        debug("OffscreenRenderer::Init - Failed to create UI buffers\n");
        return false;
    }
    
    // Set RsGlobal to logical dimensions
    RsGlobal.width = ms_renderWidth;
    RsGlobal.height = ms_renderHeight;
    RsGlobal.maximumWidth = ms_renderWidth;
    RsGlobal.maximumHeight = ms_renderHeight;
    
    ms_initialized = true;
    ms_enabled = true;

    debug("OffscreenRenderer initialized: UI %dx%d, 3D %dx%d (scale %.2f)\n",
          ms_renderWidth, ms_renderHeight,
          ms_3dWidth, ms_3dHeight, ms_3dScale);
    return true;
}

void
OffscreenRenderer::Shutdown(void)
{
    if (!ms_initialized)
        return;
    
    Destroy3DCamera();
    DestroyOffscreenBuffers();
    
    ms_initialized = false;
    ms_enabled = false;
    ms_inFrame = false;
    ms_in3D = false;
    
    debug("OffscreenRenderer shutdown\n");
}

// =============================================================================
// UI Buffer Management
// =============================================================================

bool
OffscreenRenderer::CreateOffscreenBuffers(int width, int height)
{
    ms_offscreenColor = RwRasterCreate(width, height, 0,
        rwRASTERTYPECAMERATEXTURE | rwRASTERFORMAT8888);
    
    if (ms_offscreenColor == nil) {
        debug("Failed to create offscreen color raster\n");
        return false;
    }
    
    ms_offscreenDepth = RwRasterCreate(width, height, 0, rwRASTERTYPEZBUFFER);
    
    if (ms_offscreenDepth == nil) {
        debug("Failed to create offscreen depth raster\n");
        RwRasterDestroy(ms_offscreenColor);
        ms_offscreenColor = nil;
        return false;
    }
    
    debug("Created UI buffers: %dx%d\n", width, height);
    return true;
}

void
OffscreenRenderer::DestroyOffscreenBuffers(void)
{
    if (ms_offscreenColor) {
        RwRasterDestroy(ms_offscreenColor);
        ms_offscreenColor = nil;
    }
    
    if (ms_offscreenDepth) {
        RwRasterDestroy(ms_offscreenDepth);
        ms_offscreenDepth = nil;
    }
}

// =============================================================================
// 3D Camera Management
// =============================================================================

bool
OffscreenRenderer::Create3DCamera(void)
{
    // Calculate dimensions
    ms_3dWidth = (int)(ms_renderWidth * ms_3dScale);
    ms_3dHeight = (int)(ms_renderHeight * ms_3dScale);
    
    // Ensure minimum size and even dimensions
    if (ms_3dWidth < 320) ms_3dWidth = 320;
    if (ms_3dHeight < 180) ms_3dHeight = 180;
    ms_3dWidth = (ms_3dWidth + 1) & ~1;
    ms_3dHeight = (ms_3dHeight + 1) & ~1;
    
    // Create frame
    ms_3dFrame = RwFrameCreate();
    if (!ms_3dFrame) {
        debug("Failed to create 3D frame\n");
        return false;
    }
    
    // Create camera
    ms_3dCamera = RwCameraCreate();
    if (!ms_3dCamera) {
        debug("Failed to create 3D camera\n");
        RwFrameDestroy(ms_3dFrame);
        ms_3dFrame = nil;
        return false;
    }
    
    RwCameraSetFrame(ms_3dCamera, ms_3dFrame);
    
    // Create color raster (CAMERATEXTURE for sampling)
    RwRaster *colorRaster = RwRasterCreate(ms_3dWidth, ms_3dHeight, 0,
        rwRASTERTYPECAMERATEXTURE | rwRASTERFORMAT8888);
    
    if (!colorRaster) {
        debug("Failed to create 3D color raster\n");
        RwCameraDestroy(ms_3dCamera);
        RwFrameDestroy(ms_3dFrame);
        ms_3dCamera = nil;
        ms_3dFrame = nil;
        return false;
    }
    
    // Create depth raster
    RwRaster *depthRaster = RwRasterCreate(ms_3dWidth, ms_3dHeight, 0,
        rwRASTERTYPEZBUFFER);
    
    if (!depthRaster) {
        debug("Failed to create 3D depth raster\n");
        RwRasterDestroy(colorRaster);
        RwCameraDestroy(ms_3dCamera);
        RwFrameDestroy(ms_3dFrame);
        ms_3dCamera = nil;
        ms_3dFrame = nil;
        return false;
    }
    
    RwCameraSetRaster(ms_3dCamera, colorRaster);
    RwCameraSetZRaster(ms_3dCamera, depthRaster);
    
    // Set projection type
    RwCameraSetProjection(ms_3dCamera, rwPERSPECTIVE);
    
    // Add to world
    if (Scene.world) {
        RpWorldAddCamera(Scene.world, ms_3dCamera);
    }
    
    // Setup cached blit vertices
    Setup3DBlitQuad();
    
    debug("Created 3D camera: %dx%d (scale %.2f)\n", 
          ms_3dWidth, ms_3dHeight, ms_3dScale);
    
    return true;
}

void
OffscreenRenderer::Destroy3DCamera(void)
{
    if (ms_3dCamera) {
        // Remove from world
        if (Scene.world) {
            RpWorldRemoveCamera(Scene.world, ms_3dCamera);
        }
        
        // Destroy rasters
        RwRaster *color = RwCameraGetRaster(ms_3dCamera);
        RwRaster *depth = RwCameraGetZRaster(ms_3dCamera);
        
        if (color) RwRasterDestroy(color);
        if (depth) RwRasterDestroy(depth);
        
        RwCameraDestroy(ms_3dCamera);
        ms_3dCamera = nil;
    }
    
    if (ms_3dFrame) {
        RwFrameDestroy(ms_3dFrame);
        ms_3dFrame = nil;
    }
    
    ms_3dWidth = 0;
    ms_3dHeight = 0;
}

void
OffscreenRenderer::Setup3DBlitQuad(void)
{
    // Full UI buffer dimensions
    float w = (float)ms_renderWidth;
    float h = (float)ms_renderHeight;
    
    // Fixed Z values for 2D blit
    float nearZ = 0.0f;
    float recipZ = 1.0f;
    
    // Top-left
    RwIm2DVertexSetScreenX(&ms_3dBlitVerts[0], 0.0f);
    RwIm2DVertexSetScreenY(&ms_3dBlitVerts[0], 0.0f);
    RwIm2DVertexSetScreenZ(&ms_3dBlitVerts[0], nearZ);
    RwIm2DVertexSetRecipCameraZ(&ms_3dBlitVerts[0], recipZ);
    RwIm2DVertexSetIntRGBA(&ms_3dBlitVerts[0], 255, 255, 255, 255);
    RwIm2DVertexSetU(&ms_3dBlitVerts[0], 0.0f, recipZ);
    RwIm2DVertexSetV(&ms_3dBlitVerts[0], 0.0f, recipZ);
    
    // Top-right
    RwIm2DVertexSetScreenX(&ms_3dBlitVerts[1], w);
    RwIm2DVertexSetScreenY(&ms_3dBlitVerts[1], 0.0f);
    RwIm2DVertexSetScreenZ(&ms_3dBlitVerts[1], nearZ);
    RwIm2DVertexSetRecipCameraZ(&ms_3dBlitVerts[1], recipZ);
    RwIm2DVertexSetIntRGBA(&ms_3dBlitVerts[1], 255, 255, 255, 255);
    RwIm2DVertexSetU(&ms_3dBlitVerts[1], 1.0f, recipZ);
    RwIm2DVertexSetV(&ms_3dBlitVerts[1], 0.0f, recipZ);
    
    // Bottom-right
    RwIm2DVertexSetScreenX(&ms_3dBlitVerts[2], w);
    RwIm2DVertexSetScreenY(&ms_3dBlitVerts[2], h);
    RwIm2DVertexSetScreenZ(&ms_3dBlitVerts[2], nearZ);
    RwIm2DVertexSetRecipCameraZ(&ms_3dBlitVerts[2], recipZ);
    RwIm2DVertexSetIntRGBA(&ms_3dBlitVerts[2], 255, 255, 255, 255);
    RwIm2DVertexSetU(&ms_3dBlitVerts[2], 1.0f, recipZ);
    RwIm2DVertexSetV(&ms_3dBlitVerts[2], 1.0f, recipZ);
    
    // Bottom-left
    RwIm2DVertexSetScreenX(&ms_3dBlitVerts[3], 0.0f);
    RwIm2DVertexSetScreenY(&ms_3dBlitVerts[3], h);
    RwIm2DVertexSetScreenZ(&ms_3dBlitVerts[3], nearZ);
    RwIm2DVertexSetRecipCameraZ(&ms_3dBlitVerts[3], recipZ);
    RwIm2DVertexSetIntRGBA(&ms_3dBlitVerts[3], 255, 255, 255, 255);
    RwIm2DVertexSetU(&ms_3dBlitVerts[3], 0.0f, recipZ);
    RwIm2DVertexSetV(&ms_3dBlitVerts[3], 1.0f, recipZ);
}

// =============================================================================
// Frame Control
// =============================================================================

void
OffscreenRenderer::BeginFrame(void)
{
    if (!ms_initialized || !ms_enabled || ms_inFrame)
        return;
    
    if (Scene.camera == nil)
        return;
    
    RwCamera *rwCam = Scene.camera;
    
    // Save original buffers
    ms_origColor = RwCameraGetRaster(rwCam);
    ms_origDepth = RwCameraGetZRaster(rwCam);
    
    // Redirect to UI offscreen buffer
    RwCameraSetRaster(rwCam, ms_offscreenColor);
    RwCameraSetZRaster(rwCam, ms_offscreenDepth);
    
    ms_inFrame = true;
}

void
OffscreenRenderer::EndFrame(void)
{
#ifdef TOUCH_CONTROLS
    TouchControls::Draw();
#endif

    if (!ms_initialized || !ms_enabled || !ms_inFrame)
        return;
    
    if (Scene.camera == nil)
        return;
    
    RwCamera *rwCam = Scene.camera;
    
    // Restore original buffers
    RwCameraSetRaster(rwCam, ms_origColor);
    RwCameraSetZRaster(rwCam, ms_origDepth);
    
    ms_inFrame = false;
    
    // Blit UI buffer to screen with rotation
    BlitToScreen();
}

// =============================================================================
// 3D Rendering
// =============================================================================

void
OffscreenRenderer::Begin3D(void)
{
    // Lazy init
    if (!ms_3dCamera && ms_3dScale <= 1.0f && Scene.world != nil) {
        Create3DCamera();
    }
    
    if (!ms_3dCamera || ms_in3D)
        return;
    
    // // End update on current camera
    RwCameraEndUpdate(Scene.camera);

    // Copy camera parameters
    RwCameraSetNearClipPlane(ms_3dCamera, RwCameraGetNearClipPlane(Scene.camera));
    RwCameraSetFarClipPlane(ms_3dCamera, RwCameraGetFarClipPlane(Scene.camera));
    RwCameraSetFogDistance(ms_3dCamera, RwCameraGetFogDistance(Scene.camera));
    
    const RwV2d *vw = RwCameraGetViewWindow(Scene.camera);
    const RwV2d *vo = RwCameraGetViewOffset(Scene.camera);
    RwCameraSetViewWindow(ms_3dCamera, vw);
    RwCameraSetViewOffset(ms_3dCamera, vo);
    
    // Copy frame transform
    RwFrame *mainFrame = RwCameraGetFrame(Scene.camera);
    RwMatrix *mainMat = RwFrameGetLTM(mainFrame);
    RwFrameTransform(ms_3dFrame, mainMat, rwCOMBINEREPLACE);
    
    // // Save and swap
    ms_savedSceneCamera = Scene.camera;
    Scene.camera = ms_3dCamera;
    
    // // Save and update RsGlobal
    ms_savedRsWidth = RsGlobal.width;
    ms_savedRsHeight = RsGlobal.height;
    RsGlobal.width = ms_3dWidth;
    RsGlobal.height = ms_3dHeight;

    RwRect rect {0, 0, ms_3dWidth, ms_3dHeight};

    // Begin update on 3D camera
    RwCameraBeginUpdate(ms_3dCamera);

    TheCamera.m_viewMatrix.Update();
    // Render sky background first
    if(CWeather::LightningFlash && !CCullZones::CamNoRain())
        CClouds::RenderBackground(255, 255, 255, 255, 255, 255, 255);
    else
        CClouds::RenderBackground(
            CTimeCycle::GetSkyTopRed(), CTimeCycle::GetSkyTopGreen(), CTimeCycle::GetSkyTopBlue(),
            CTimeCycle::GetSkyBottomRed(), CTimeCycle::GetSkyBottomGreen(), CTimeCycle::GetSkyBottomBlue(), 255);
    
    CClouds::RenderHorizon();

    // Clear Z buffer after sky rendering to avoid render artifacts
    CRGBA clearColor(CTimeCycle::GetSkyBottomRed(), CTimeCycle::GetSkyBottomGreen(), CTimeCycle::GetSkyBottomBlue(), 255);
    RwCameraClear(ms_3dCamera, &clearColor.rwRGBA, rwCAMERACLEARZ); 

    ms_in3D = true;
}

void
OffscreenRenderer::End3D(void)
{
    if (!ms_in3D || !ms_savedSceneCamera)
        return;
    
    // CRITICAL: End update on 3D camera
    RwCameraEndUpdate(ms_3dCamera);
    
    // Restore main camera
    Scene.camera = ms_savedSceneCamera;
    ms_savedSceneCamera = nil;
    
    // Restore RsGlobal
    RsGlobal.width = ms_savedRsWidth;
    RsGlobal.height = ms_savedRsHeight;
    
    ms_in3D = false;
    
    // Blit 3D result to UI buffer (this does its own BeginUpdate/EndUpdate)
    Blit3DToUI();
    
    // CRITICAL: Resume update on main camera for UI rendering
    RwCameraBeginUpdate(Scene.camera);
}

void
OffscreenRenderer::Blit3DToUI(void)
{
    RwRaster *src = RwCameraGetRaster(ms_3dCamera);
    if (!src)
        return;
    
    if (!RwCameraBeginUpdate(Scene.camera))
        return;
    
    // Set render states
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, src);
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSCLAMP);
    
    // Draw cached quad
    RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, ms_3dBlitVerts, 4, ms_3dBlitIndices, 6);
    
    // Restore states
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nil);
    
    RwCameraEndUpdate(Scene.camera);
}

// =============================================================================
// Screen Blit (UI -> Physical Screen with rotation)
// =============================================================================

void
OffscreenRenderer::SetupBlitQuad(void)
{
    float screenW = (float)SCREEN_WIDTH;
    float screenH = (float)SCREEN_HEIGHT;
    float nearZ = RwIm2DGetNearScreenZ();
    float recipZ = 1.0f / RwCameraGetNearClipPlane(Scene.camera);
    
    // UV coordinates based on rotation
    float u0, v0, u1, v1, u2, v2, u3, v3;
    
    switch (ms_rotation) {
    case ROTATE_0:
        u0 = 0.0f; v0 = 0.0f;
        u1 = 1.0f; v1 = 0.0f;
        u2 = 1.0f; v2 = 1.0f;
        u3 = 0.0f; v3 = 1.0f;
        break;
        
    case ROTATE_90:
        u0 = 0.0f; v0 = 1.0f;
        u1 = 0.0f; v1 = 0.0f;
        u2 = 1.0f; v2 = 0.0f;
        u3 = 1.0f; v3 = 1.0f;
        break;
        
    case ROTATE_180:
        u0 = 1.0f; v0 = 1.0f;
        u1 = 0.0f; v1 = 1.0f;
        u2 = 0.0f; v2 = 0.0f;
        u3 = 1.0f; v3 = 0.0f;
        break;
        
    case ROTATE_270:
        u0 = 1.0f; v0 = 0.0f;
        u1 = 1.0f; v1 = 1.0f;
        u2 = 0.0f; v2 = 1.0f;
        u3 = 0.0f; v3 = 0.0f;
        break;
    }
    
    // Top-left
    RwIm2DVertexSetScreenX(&ms_screenBlitVerts[0], 0.0f);
    RwIm2DVertexSetScreenY(&ms_screenBlitVerts[0], 0.0f);
    RwIm2DVertexSetScreenZ(&ms_screenBlitVerts[0], nearZ);
    RwIm2DVertexSetRecipCameraZ(&ms_screenBlitVerts[0], recipZ);
    RwIm2DVertexSetIntRGBA(&ms_screenBlitVerts[0], 255, 255, 255, 255);
    RwIm2DVertexSetU(&ms_screenBlitVerts[0], u0, recipZ);
    RwIm2DVertexSetV(&ms_screenBlitVerts[0], v0, recipZ);
    
    // Top-right
    RwIm2DVertexSetScreenX(&ms_screenBlitVerts[1], screenW);
    RwIm2DVertexSetScreenY(&ms_screenBlitVerts[1], 0.0f);
    RwIm2DVertexSetScreenZ(&ms_screenBlitVerts[1], nearZ);
    RwIm2DVertexSetRecipCameraZ(&ms_screenBlitVerts[1], recipZ);
    RwIm2DVertexSetIntRGBA(&ms_screenBlitVerts[1], 255, 255, 255, 255);
    RwIm2DVertexSetU(&ms_screenBlitVerts[1], u1, recipZ);
    RwIm2DVertexSetV(&ms_screenBlitVerts[1], v1, recipZ);
    
    // Bottom-right
    RwIm2DVertexSetScreenX(&ms_screenBlitVerts[2], screenW);
    RwIm2DVertexSetScreenY(&ms_screenBlitVerts[2], screenH);
    RwIm2DVertexSetScreenZ(&ms_screenBlitVerts[2], nearZ);
    RwIm2DVertexSetRecipCameraZ(&ms_screenBlitVerts[2], recipZ);
    RwIm2DVertexSetIntRGBA(&ms_screenBlitVerts[2], 255, 255, 255, 255);
    RwIm2DVertexSetU(&ms_screenBlitVerts[2], u2, recipZ);
    RwIm2DVertexSetV(&ms_screenBlitVerts[2], v2, recipZ);
    
    // Bottom-left
    RwIm2DVertexSetScreenX(&ms_screenBlitVerts[3], 0.0f);
    RwIm2DVertexSetScreenY(&ms_screenBlitVerts[3], screenH);
    RwIm2DVertexSetScreenZ(&ms_screenBlitVerts[3], nearZ);
    RwIm2DVertexSetRecipCameraZ(&ms_screenBlitVerts[3], recipZ);
    RwIm2DVertexSetIntRGBA(&ms_screenBlitVerts[3], 255, 255, 255, 255);
    RwIm2DVertexSetU(&ms_screenBlitVerts[3], u3, recipZ);
    RwIm2DVertexSetV(&ms_screenBlitVerts[3], v3, recipZ);
}

void
OffscreenRenderer::BlitToScreen(void)
{
    if (ms_offscreenColor == nil)
        return;
    
    if (!RwCameraBeginUpdate(Scene.camera))
        return;
    
    // Setup fullscreen quad with rotation
    SetupBlitQuad();
    
    // Set render states
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, ms_offscreenColor);
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSCLAMP);
    
    // Render fullscreen quad
    RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, ms_screenBlitVerts, 4, ms_screenBlitIndices, 6);
    
    // Restore states
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nil);
    
    RwCameraEndUpdate(Scene.camera);
}

// =============================================================================
// Configuration
// =============================================================================

bool
OffscreenRenderer::Resize(int newWidth, int newHeight)
{
    if (!ms_initialized)
        return false;
    
    if (ms_inFrame) {
        debug("Cannot resize while in frame!\n");
        return false;
    }
    
    DestroyOffscreenBuffers();
    
    ms_renderWidth = newWidth;
    ms_renderHeight = newHeight;
    
    if (!CreateOffscreenBuffers(newWidth, newHeight))
        return false;
    
    // Recreate 3D camera with new proportions if needed
    if (ms_3dCamera && ms_3dScale <= 1.0f) {
        Destroy3DCamera();
        Create3DCamera();
    }
    
    return true;
}

void
OffscreenRenderer::Set3DResolution(float scale)
{
    if (scale < 0.25f) scale = 0.25f;
    if (scale > 1.0f) scale = 1.0f;
    
    if (scale == ms_3dScale)
        return;
    
    ms_3dScale = scale;
    
    if (!ms_initialized)
        return;
    
    // Destroy existing 3D camera
    Destroy3DCamera();
    
    // Create new one if scaling is enabled
    if (scale < 1.0f) {
        Create3DCamera();
    }
}

static bool
IsRotationSideways(OffscreenRenderer::Rotation rot)
{
    return rot == OffscreenRenderer::ROTATE_90 || 
           rot == OffscreenRenderer::ROTATE_270;
}

void
OffscreenRenderer::SetRotation(Rotation rot)
{
    if (rot == ms_rotation)
        return;
    
    bool wasSideways = IsRotationSideways(ms_rotation);
    bool isSideways = IsRotationSideways(rot);
    
    ms_rotation = rot;
    
    // If orientation changed, resize buffer
    if (wasSideways != isSideways) {
        int newW = ms_renderHeight;
        int newH = ms_renderWidth;
        
        debug("Rotation changed orientation: resizing buffer %dx%d -> %dx%d\n",
              ms_renderWidth, ms_renderHeight, newW, newH);
        if (ms_initialized) {
            Resize(newW, newH);
        }
    }

    // Inform Wayland compositor
    GLFWwindow* window = glfwGetCurrentContext();
    if (window) {
        int transform;
        switch (rot) {
        case ROTATE_0:   transform = GLFW_TRANSFORM_NORMAL; break;
        case ROTATE_90:  transform = GLFW_TRANSFORM_270;    break;
        case ROTATE_180: transform = GLFW_TRANSFORM_180;    break;
        case ROTATE_270: transform = GLFW_TRANSFORM_90;     break;
        default:         transform = GLFW_TRANSFORM_NORMAL; break;
        }
        glfwSetWindowContentTransform(window, transform);
    }
}

void
OffscreenRenderer::SetRotation(int degrees)
{
    degrees = ((degrees % 360) + 360) % 360;
    
    Rotation rot;
    switch (degrees) {
    case 0:   rot = ROTATE_0;   break;
    case 90:  rot = ROTATE_90;  break;
    case 180: rot = ROTATE_180; break;
    case 270: rot = ROTATE_270; break;
    default:
        if (degrees < 45)        rot = ROTATE_0;
        else if (degrees < 135)  rot = ROTATE_90;
        else if (degrees < 225)  rot = ROTATE_180;
        else if (degrees < 315)  rot = ROTATE_270;
        else                     rot = ROTATE_0;
        break;
    }
    
    SetRotation(rot);
}

void
OffscreenRenderer::UpdateRotation(int transform)
{
    switch (transform) {
    case GLFW_TRANSFORM_NORMAL:
    case GLFW_TRANSFORM_270:
        SetRotation(ROTATE_90);
        break;
    case GLFW_TRANSFORM_180:
    case GLFW_TRANSFORM_90:
        SetRotation(ROTATE_270);
        break;
    }
}

void
OffscreenRenderer::FlipRotation(void)
{
    switch (ms_rotation) {
    case ROTATE_0:   ms_rotation = ROTATE_180; break;
    case ROTATE_90:  ms_rotation = ROTATE_270; break;
    case ROTATE_180: ms_rotation = ROTATE_0;   break;
    case ROTATE_270: ms_rotation = ROTATE_90;  break;
    }
}

void
OffscreenRenderer::SetEnabled(bool enabled)
{
    if (ms_inFrame && !enabled) {
        EndFrame();
    }
    ms_enabled = enabled;
}

// =============================================================================
// Input Coordinate Transform
// =============================================================================

void
OffscreenRenderer::TransformInputCoords(float windowX, float windowY,
                                        float *gameX, float *gameY)
{
    if (!ms_initialized || !ms_enabled) {
        *gameX = windowX;
        *gameY = windowY;
        return;
    }
    
    float winW = (float)ms_windowWidth;
    float winH = (float)ms_windowHeight;
    float gameW = (float)ms_renderWidth;
    float gameH = (float)ms_renderHeight;
    
    float tx = windowX;
    float ty = windowY;
    
    switch (ms_rotation) {
    case ROTATE_0:
        *gameX = tx * gameW / winW;
        *gameY = ty * gameH / winH;
        break;
        
    case ROTATE_90:
        *gameX = ty * gameW / winH;
        *gameY = (winW - tx) * gameH / winW;
        break;
        
    case ROTATE_180:
        *gameX = (winW - tx) * gameW / winW;
        *gameY = (winH - ty) * gameH / winH;
        break;
        
    case ROTATE_270:
        *gameX = (winH - ty) * gameW / winH;
        *gameY = tx * gameH / winW;
        break;
    }
}

void
OffscreenRenderer::TransformInputCoords(double *windowX, double *windowY)
{
    float gx, gy;
    TransformInputCoords((float)*windowX, (float)*windowY, &gx, &gy);
    *windowX = (double)gx;
    *windowY = (double)gy;
}

#endif // OFFSCREEN_RENDER
