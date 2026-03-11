/*
 * OffscreenRenderer.cpp - Offscreen rendering implementation
 * 
 */

#ifdef OFFSCREEN_RENDER

#include "common.h"
#include "main.h"
#include "Camera.h"
#include "Draw.h"
#include "RwHelper.h"
#include "OffscreenRenderer.h"
#include <GLFW/glfw3.h>

// Static member initialization
RwRaster *OffscreenRenderer::ms_offscreenColor = nil;
RwRaster *OffscreenRenderer::ms_offscreenDepth = nil;
RwRaster *OffscreenRenderer::ms_origColor = nil;
RwRaster *OffscreenRenderer::ms_origDepth = nil;

bool OffscreenRenderer::ms_initialized = false;
bool OffscreenRenderer::ms_enabled = true;
bool OffscreenRenderer::ms_inFrame = false;

int OffscreenRenderer::ms_renderWidth = 0;
int OffscreenRenderer::ms_renderHeight = 0;
int OffscreenRenderer::ms_windowWidth = 0;
int OffscreenRenderer::ms_windowHeight = 0;
float OffscreenRenderer::ms_renderAspect = 16.0f/9.0f;
float OffscreenRenderer::ms_renderScale = 1.0f;
OffscreenRenderer::Rotation OffscreenRenderer::ms_rotation = ROTATE_0;

// Fullscreen quad vertices and indices
static RwIm2DVertex blitVerts[4];
static RwImVertexIndex blitIndices[6] = { 0, 1, 2, 0, 2, 3 };

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
        ms_renderWidth = winH;   // swap dimensions for landscape FBO
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
    
    int wl_transform = GLFW_TRANSFORM_NORMAL;
    switch (ms_rotation) {
    case ROTATE_0:
        wl_transform = GLFW_TRANSFORM_NORMAL;
        break;
    case ROTATE_90:
        wl_transform = GLFW_TRANSFORM_270;
        break;
    case ROTATE_180:
        wl_transform = GLFW_TRANSFORM_180;
        break;
    case ROTATE_270:
        wl_transform = GLFW_TRANSFORM_90;
        break;
    default:
        wl_transform = GLFW_TRANSFORM_NORMAL;
        break;
    }
    glfwSetWindowContentTransform(window, wl_transform);
    
    
    ms_renderAspect = (float)ms_renderWidth / (float)ms_renderHeight;

    // Create offscreen FBO with logical (landscape) dimensions
    if (!CreateOffscreenBuffers(ms_renderWidth, ms_renderHeight)) {
        debug("OffscreenRenderer::Init - Failed to create buffers\n");
        return false;
    }
    
    // Set RsGlobal to logical dimensions - this makes the game think
    // it's running in landscape mode regardless of actual window orientation
    RsGlobal.width = ms_renderWidth;
    RsGlobal.height = ms_renderHeight;
    RsGlobal.maximumWidth = ms_renderWidth;
    RsGlobal.maximumHeight = ms_renderHeight;
    
    ms_initialized = true;
    ms_enabled = true;

    debug("OffscreenRenderer initialized: FBO %dx%d, RsGlobal set to %dx%d\n",
          ms_renderWidth, ms_renderHeight,
          RsGlobal.maximumWidth, RsGlobal.maximumHeight);
    return true;
}

void
OffscreenRenderer::Shutdown(void)
{
    if (!ms_initialized)
        return;
    
    DestroyOffscreenBuffers();
    
    ms_initialized = false;
    ms_enabled = false;
    ms_inFrame = false;
    
    debug("OffscreenRenderer shutdown\n");
}

bool
OffscreenRenderer::CreateOffscreenBuffers(int width, int height)
{
    // Create color buffer (CAMERATEXTURE = can be sampled as texture)
    // rwRASTERTYPECAMERATEXTURE = 5, rwRASTERFORMAT8888 = 0x500
    ms_offscreenColor = RwRasterCreate(width, height, 0,
        rwRASTERTYPECAMERATEXTURE | rwRASTERFORMAT8888);
    
    if (ms_offscreenColor == nil) {
        debug("Failed to create offscreen color raster\n");
        return false;
    }
    
    // Create depth buffer
    // rwRASTERTYPEZBUFFER = 1
    // librw automatically uses GL_DEPTH24_STENCIL8 internally
    ms_offscreenDepth = RwRasterCreate(width, height, 0,
        rwRASTERTYPEZBUFFER);
    
    if (ms_offscreenDepth == nil) {
        debug("Failed to create offscreen depth raster\n");
        RwRasterDestroy(ms_offscreenColor);
        ms_offscreenColor = nil;
        return false;
    }
    
    debug("Created offscreen buffers: %dx%d\n", width, height);
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
    
    // Redirect to offscreen
    RwCameraSetRaster(rwCam, ms_offscreenColor);
    RwCameraSetZRaster(rwCam, ms_offscreenDepth);
    
    ms_inFrame = true;
}

void
OffscreenRenderer::EndFrame(void)
{
    if (!ms_initialized || !ms_enabled || !ms_inFrame)
        return;
    
    if (Scene.camera == nil)
        return;
    
    RwCamera *rwCam = Scene.camera;
    
    // Restore original buffers
    RwCameraSetRaster(rwCam, ms_origColor);
    RwCameraSetZRaster(rwCam, ms_origDepth);
    
    ms_inFrame = false;
    
    // Blit offscreen to screen
    BlitToScreen();
}

void
OffscreenRenderer::SetupBlitQuad(void)
{
    float screenW = (float)SCREEN_WIDTH;
    float screenH = (float)SCREEN_HEIGHT;
    float nearZ = RwIm2DGetNearScreenZ();
    float recipZ = 1.0f / RwCameraGetNearClipPlane(Scene.camera);
    
    // UV coordinates based on rotation
    // OpenGL renders bottom-up, so we flip V by default
    float u0, v0, u1, v1, u2, v2, u3, v3;
    
    switch (ms_rotation) {
    case ROTATE_0:
        // No rotation, direct mapping
        u0 = 0.0f; v0 = 0.0f;  // TL screen
        u1 = 1.0f; v1 = 0.0f;  // TR screen
        u2 = 1.0f; v2 = 1.0f;  // BR screen
        u3 = 0.0f; v3 = 1.0f;  // BL screen
        break;
        
    case ROTATE_90:
        // 90° CW
        u0 = 0.0f; v0 = 1.0f;
        u1 = 0.0f; v1 = 0.0f;
        u2 = 1.0f; v2 = 0.0f;
        u3 = 1.0f; v3 = 1.0f;
        break;
        
    case ROTATE_180:
        // 180°
        u0 = 1.0f; v0 = 1.0f;
        u1 = 0.0f; v1 = 1.0f;
        u2 = 0.0f; v2 = 0.0f;
        u3 = 1.0f; v3 = 0.0f;
        break;
        
    case ROTATE_270:
        // 270° CW (90° CCW)
        u0 = 1.0f; v0 = 0.0f;
        u1 = 1.0f; v1 = 1.0f;
        u2 = 0.0f; v2 = 1.0f;
        u3 = 0.0f; v3 = 0.0f;
        break;
    }
    
    // Top-left vertex
    RwIm2DVertexSetScreenX(&blitVerts[0], 0.0f);
    RwIm2DVertexSetScreenY(&blitVerts[0], 0.0f);
    RwIm2DVertexSetScreenZ(&blitVerts[0], nearZ);
    RwIm2DVertexSetRecipCameraZ(&blitVerts[0], recipZ);
    RwIm2DVertexSetIntRGBA(&blitVerts[0], 255, 255, 255, 255);
    RwIm2DVertexSetU(&blitVerts[0], u0, recipZ);
    RwIm2DVertexSetV(&blitVerts[0], v0, recipZ);
    
    // Top-right vertex
    RwIm2DVertexSetScreenX(&blitVerts[1], screenW);
    RwIm2DVertexSetScreenY(&blitVerts[1], 0.0f);
    RwIm2DVertexSetScreenZ(&blitVerts[1], nearZ);
    RwIm2DVertexSetRecipCameraZ(&blitVerts[1], recipZ);
    RwIm2DVertexSetIntRGBA(&blitVerts[1], 255, 255, 255, 255);
    RwIm2DVertexSetU(&blitVerts[1], u1, recipZ);
    RwIm2DVertexSetV(&blitVerts[1], v1, recipZ);
    
    // Bottom-right vertex
    RwIm2DVertexSetScreenX(&blitVerts[2], screenW);
    RwIm2DVertexSetScreenY(&blitVerts[2], screenH);
    RwIm2DVertexSetScreenZ(&blitVerts[2], nearZ);
    RwIm2DVertexSetRecipCameraZ(&blitVerts[2], recipZ);
    RwIm2DVertexSetIntRGBA(&blitVerts[2], 255, 255, 255, 255);
    RwIm2DVertexSetU(&blitVerts[2], u2, recipZ);
    RwIm2DVertexSetV(&blitVerts[2], v2, recipZ);
    
    // Bottom-left vertex
    RwIm2DVertexSetScreenX(&blitVerts[3], 0.0f);
    RwIm2DVertexSetScreenY(&blitVerts[3], screenH);
    RwIm2DVertexSetScreenZ(&blitVerts[3], nearZ);
    RwIm2DVertexSetRecipCameraZ(&blitVerts[3], recipZ);
    RwIm2DVertexSetIntRGBA(&blitVerts[3], 255, 255, 255, 255);
    RwIm2DVertexSetU(&blitVerts[3], u3, recipZ);
    RwIm2DVertexSetV(&blitVerts[3], v3, recipZ);
}

void
OffscreenRenderer::BlitToScreen(void)
{
    if (ms_offscreenColor == nil)
        return;
    
    // We need to render to the screen, so begin update on original camera
    // This should be called AFTER RwCameraEndUpdate but BEFORE RsCameraShowRaster
    // At this point the camera is not in update mode, so we do a quick begin/end
    
    if (!RwCameraBeginUpdate(Scene.camera))
        return;
    
    // Setup fullscreen quad with rotation
    SetupBlitQuad();
    
    // Set render states for blitting
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATESRCBLEND, (void*)rwBLENDONE);
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, (void*)rwBLENDZERO);
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, (void*)FALSE);
    RwRenderStateSet(rwRENDERSTATECULLMODE, (void*)rwCULLMODECULLNONE);
    
    // Set the offscreen color buffer as texture
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, ms_offscreenColor);
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, (void*)rwFILTERLINEAR);
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, (void*)rwTEXTUREADDRESSCLAMP);
    
    // Render fullscreen quad
    RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, blitVerts, 4, blitIndices, 6);
    
    // Restore some states
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, (void*)TRUE);
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nil);
    
    RwCameraEndUpdate(Scene.camera);
}

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
    
    return CreateOffscreenBuffers(newWidth, newHeight);
}

// Helper: check if rotation is "sideways" (90 or 270)
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
    
    // If orientation changed (landscape <-> portrait), resize buffer
    // 0/180 <-> 90/270 requires buffer resize (swap dimensions)
    if (wasSideways != isSideways) {
        // Swap width and height
        int newW = ms_renderHeight;
        int newH = ms_renderWidth;
        
        debug("Rotation changed orientation: resizing buffer %dx%d -> %dx%d\n",
              ms_renderWidth, ms_renderHeight, newW, newH);
        if (ms_initialized) {
            Resize(newW, newH);
        }
    }
    // else: 0<->180 or 90<->270 - just UV change, no resize needed

    // Inform Wayland compositor about buffer transform
    GLFWwindow* window = glfwGetCurrentContext();
    if (window) {
        int transform;
        switch (rot) {
        case ROTATE_0:   transform = GLFW_TRANSFORM_NORMAL; break;
        case ROTATE_90: transform = GLFW_TRANSFORM_270;    break;
        case ROTATE_180: transform = GLFW_TRANSFORM_180;    break;
        case ROTATE_270:  transform = GLFW_TRANSFORM_90;     break;
        default:         transform = GLFW_TRANSFORM_NORMAL; break;
        }
        glfwSetWindowContentTransform(window, transform);
    }
}

void
OffscreenRenderer::SetRotation(int degrees)
{
    // Normalize to valid values
    degrees = ((degrees % 360) + 360) % 360;  // Handle negative
    
    Rotation rot;
    switch (degrees) {
    case 0:   rot = ROTATE_0;   break;
    case 90:  rot = ROTATE_90;  break;
    case 180: rot = ROTATE_180; break;
    case 270: rot = ROTATE_270; break;
    default:
        // Snap to nearest 90 degrees
        if (degrees < 45)        rot = ROTATE_0;
        else if (degrees < 135)  rot = ROTATE_90;
        else if (degrees < 225)  rot = ROTATE_180;
        else if (degrees < 315)  rot = ROTATE_270;
        else                     rot = ROTATE_0;
        break;
    }
    
    SetRotation(rot);  // Use main function for resize logic
}

void
OffscreenRenderer::UpdateRotation(int transform)
{
    switch (transform)
    {
    case GLFW_TRANSFORM_NORMAL:
    case GLFW_TRANSFORM_270:
        OffscreenRenderer::SetRotation(OffscreenRenderer::ROTATE_90);
        break;
    case GLFW_TRANSFORM_180:
    case GLFW_TRANSFORM_90:
        OffscreenRenderer::SetRotation(OffscreenRenderer::ROTATE_270);
        break;
    }
}

void
OffscreenRenderer::FlipRotation(void)
{
    // Add 180 degrees: 0<->180, 90<->270
    // This NEVER requires buffer resize (same orientation)
    switch (ms_rotation) {
    case ROTATE_0:   ms_rotation = ROTATE_180; break;
    case ROTATE_90:  ms_rotation = ROTATE_270; break;
    case ROTATE_180: ms_rotation = ROTATE_0;   break;
    case ROTATE_270: ms_rotation = ROTATE_90;  break;
    }
    // No resize needed - just UV flip
}

void
OffscreenRenderer::SetRenderScale(float scale)
{
    if (scale < 0.25f) scale = 0.25f;
    if (scale > 4.0f) scale = 4.0f;
    
    ms_renderScale = scale;
    
    // Optionally resize buffers based on scale
    // int newW = (int)(SCREEN_WIDTH * scale);
    // int newH = (int)(SCREEN_HEIGHT * scale);
    // Resize(newW, newH);
}

void
OffscreenRenderer::SetEnabled(bool enabled)
{
    if (ms_inFrame && !enabled) {
        // Finish current frame first
        EndFrame();
    }
    ms_enabled = enabled;
}

void
OffscreenRenderer::TransformInputCoords(float windowX, float windowY,
                                        float *gameX, float *gameY)
{
    if (!ms_initialized || !ms_enabled) {
        *gameX = windowX;
        *gameY = windowY;
        return;
    }
    
    // Window dimensions (actual pixels on screen)
    float winW = (float)ms_windowWidth;
    float winH = (float)ms_windowHeight;
    
    // Game dimensions (logical render size)
    float gameW = (float)ms_renderWidth;
    float gameH = (float)ms_renderHeight;
    
    // Transform based on rotation
    // Input is in window coordinates, output is in game coordinates
    float tx = windowX;
    float ty = windowY;
    
    switch (ms_rotation) {
    case ROTATE_0:
        // No rotation: just scale
        *gameX = tx * gameW / winW;
        *gameY = ty * gameH / winH;
        break;
        
    case ROTATE_90:
        // Window is portrait, game is landscape rotated 90 CW
        // Window (0,0) = top-left -> Game (0, gameH)
        // Window (winW,0) = top-right -> Game (0, 0)
        // Window (0,winH) = bottom-left -> Game (gameW, gameH)
        *gameX = ty * gameW / winH;
        *gameY = (winW - tx) * gameH / winW;
        break;
        
    case ROTATE_180:
        // Upside down
        *gameX = (winW - tx) * gameW / winW;
        *gameY = (winH - ty) * gameH / winH;
        break;
        
    case ROTATE_270:
        // Window is portrait, game is landscape rotated 270 CW (90 CCW)
        // Window (0,0) = top-left -> Game (gameW, 0)
        // Window (winW,0) = top-right -> Game (gameW, gameH)
        // Window (0,winH) = bottom-left -> Game (0, 0)
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