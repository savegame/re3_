/*
 * OffscreenRenderer.h - Offscreen rendering with rotation/scaling support
 * 
 * This module allows rendering the entire game to an offscreen framebuffer,
 * then blitting it to screen with optional rotation (0/90/180/270 degrees)
 * and scaling. Useful for mobile devices with different orientations.
 *
 * Usage:
 *   1. Call OffscreenRenderer::Init() after RenderWare initialization
 *   2. Call OffscreenRenderer::BeginFrame() before RwCameraBeginUpdate
 *   3. Call OffscreenRenderer::EndFrame() after RwCameraEndUpdate (before ShowRaster)
 *   4. Call OffscreenRenderer::Shutdown() on exit
 */

#ifndef OFFSCREEN_RENDERER_H
#define OFFSCREEN_RENDERER_H

#ifdef OFFSCREEN_RENDER

// Forward declarations - avoid including librw headers directly
#include "common.h"

class OffscreenRenderer
{
public:
    enum Rotation {
        ROTATE_0   = 0,
        ROTATE_90  = 90,
        ROTATE_180 = 180,
        ROTATE_270 = 270
    };

    // Initialize offscreen rendering system
    // Call after Scene.camera is created in CGame::InitialiseRenderWare()
    // Automatically detects window size and orientation from GLFW
    // Sets RsGlobal to logical (landscape) dimensions
    static bool Init();
    
    // Shutdown and free resources
    static void Shutdown(void);
    
    // Call before DoRWStuffStartOfFrame - redirects rendering to offscreen
    static void BeginFrame(void);
    
    // Call after RwCameraEndUpdate, before RsCameraShowRaster
    // Blits offscreen buffer to screen with current rotation/scale
    static void EndFrame(void);
    
    // Resize offscreen buffer (e.g., for resolution scaling)
    static bool Resize(int newWidth, int newHeight);
    
    // Set rotation (0, 90, 180, 270 degrees)
    // Can be changed at any time - takes effect on next frame
    // NOTE: Changing between 0/180 and 90/270 will resize the buffer!
    //       Changing within same orientation (0<->180 or 90<->270) is instant.
    static void SetRotation(Rotation rot);
    static void SetRotation(int degrees);  // Convenience: accepts 0, 90, 180, 270
    static Rotation GetRotation(void) { return ms_rotation; }
    static int GetRotationDegrees(void) { return (int)ms_rotation; }
    static void UpdateRotation(int monitorTransform);
    
    // Flip rotation by 180 degrees (90<->270 or 0<->180)
    // This is ALWAYS instant - no buffer resize needed
    // Useful for device flip without orientation change
    static void FlipRotation(void);
    
    // Check if current rotation is sideways (90 or 270)
    static bool IsSideways(void) { 
        return ms_rotation == ROTATE_90 || ms_rotation == ROTATE_270; 
    }
    
    // Set render scale (0.5 = half resolution, 1.0 = full, 2.0 = super sampling)
    static void SetRenderScale(float scale);
    static float GetRenderScale(void) { return ms_renderScale; }
    
    // Enable/disable the offscreen system at runtime
    static void SetEnabled(bool enabled);
    static bool IsEnabled(void) { return ms_enabled; }
    
    // Check if initialized
    static bool IsInitialized(void) { return ms_initialized; }
    
    // Get render dimensions
    static int GetRenderWidth(void) { return ms_renderWidth; }
    static int GetRenderHeight(void) { return ms_renderHeight; }

    // Get actual aspect ration
    static int GetAspectRatio(void) { return ms_renderAspect; }

    // Get actual window dimensions (before rotation)
    static int GetWindowWidth(void) { return ms_windowWidth; }
    static int GetWindowHeight(void) { return ms_windowHeight; }
    
    // Transform screen coordinates for input (mouse/touch)
    // Takes screen coords, returns game coords accounting for rotation
    static void TransformInputCoords(float windowX, float windowY, 
                                     float *gameX, float *gameY);
    static void TransformInputCoords(double *windowX, double *windowY);

private:
    static bool CreateOffscreenBuffers(int width, int height);
    static void DestroyOffscreenBuffers(void);
    static void BlitToScreen(void);
    static void SetupBlitQuad(void);
    
    // Offscreen rasters (using RW types, not librw internal types)
    static RwRaster *ms_offscreenColor;   // CAMERATEXTURE - color buffer
    static RwRaster *ms_offscreenDepth;   // ZBUFFER - depth buffer
    
    // Original camera rasters (to restore)
    static RwRaster *ms_origColor;
    static RwRaster *ms_origDepth;
    
    // State
    static bool ms_initialized;
    static bool ms_enabled;
    static bool ms_inFrame;                 // Currently rendering to offscreen

    // Render parameters
    static int ms_renderWidth;      // logical render size (landscape)
    static int ms_renderHeight;
    static int ms_windowWidth;      // actual window size in pixels
    static int ms_windowHeight;
    static float ms_renderAspect;
    static float ms_renderScale;
    static Rotation ms_rotation;
};

#endif // OFFSCREEN_RENDER

#endif // OFFSCREEN_RENDERER_H