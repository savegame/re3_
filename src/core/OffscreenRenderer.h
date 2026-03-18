/*
 * OffscreenRenderer.h - Offscreen rendering with rotation/scaling support
 * 
 * This module allows rendering the entire game to an offscreen framebuffer,
 * then blitting it to screen with optional rotation (0/90/180/270 degrees)
 * and scaling. Useful for mobile devices with different orientations.
 *
 * Supports separate 3D and UI render targets:
 *   - 3D renders at configurable lower resolution (for performance)
 *   - UI renders at native resolution (for clarity)
 *
 * Usage:
 *   1. Call OffscreenRenderer::Init() after RenderWare initialization
 *   2. Call OffscreenRenderer::BeginFrame() before RwCameraBeginUpdate
 *   3. For 3D content: Begin3D() before RenderScene, End3D() after RenderEffects
 *   4. Call OffscreenRenderer::EndFrame() after all rendering (before ShowRaster)
 *   5. Call OffscreenRenderer::Shutdown() on exit
 */

#ifndef OFFSCREEN_RENDERER_H
#define OFFSCREEN_RENDERER_H

#ifdef OFFSCREEN_RENDER

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
    
    // =====================================================
    // 3D Rendering (separate camera at lower resolution)
    // =====================================================
    
    // Call before RenderScene() - switches to 3D camera
    // Temporarily replaces Scene.camera with internal 3D camera
    static void Begin3D(void);
    
    // Call after RenderEffects() / RenderMotionBlur() - blits 3D to UI buffer
    // Restores Scene.camera and renders 3D result onto UI buffer
    static void End3D(void);
    
    // Check if 3D camera is active
    static bool IsIn3D(void) { return ms_in3D; }
    
    // Check if 3D scaling is available (camera was created successfully)
    static bool Has3DScaling(void) { return ms_3dCamera != nil; }
    
    // Set 3D render resolution (0.25 - 1.0, default 0.5)
    // Values < 1.0 render 3D at lower resolution for performance
    // Value 1.0 disables 3D scaling (renders at full UI resolution)
    static void Set3DResolution(float scale);
    static float Get3DResolution(void) { return ms_3dScale; }
    
    // Get current 3D render dimensions
    static int Get3DWidth(void) { return ms_3dWidth; }
    static int Get3DHeight(void) { return ms_3dHeight; }
    
    // =====================================================
    // UI / Full resolution settings
    // =====================================================
    
    // Resize offscreen buffer (e.g., for resolution scaling)
    static bool Resize(int newWidth, int newHeight);
    
    // Set rotation (0, 90, 180, 270 degrees)
    static void SetRotation(Rotation rot);
    static void SetRotation(int degrees);
    static Rotation GetRotation(void) { return ms_rotation; }
    static int GetRotationDegrees(void) { return (int)ms_rotation; }
    static void UpdateRotation(int monitorTransform);
    
    // Flip rotation by 180 degrees (instant, no buffer resize)
    static void FlipRotation(void);
    
    // Check if current rotation is sideways (90 or 270)
    static bool IsSideways(void) { 
        return ms_rotation == ROTATE_90 || ms_rotation == ROTATE_270; 
    }
    
    // Enable/disable the offscreen system at runtime
    static void SetEnabled(bool enabled);
    static bool IsEnabled(void) { return ms_enabled; }
    
    // Check if initialized
    static bool IsInitialized(void) { return ms_initialized; }
    
    // Get render dimensions (UI buffer / logical size)
    static int GetRenderWidth(void) { return ms_renderWidth; }
    static int GetRenderHeight(void) { return ms_renderHeight; }

    // Get actual aspect ratio
    static float GetAspectRatio(void) { return ms_renderAspect; }

    // Get actual window dimensions (before rotation)
    static int GetWindowWidth(void) { return ms_windowWidth; }
    static int GetWindowHeight(void) { return ms_windowHeight; }
    
    // Transform screen coordinates for input (mouse/touch)
    static void TransformInputCoords(float windowX, float windowY, 
                                     float *gameX, float *gameY);
    static void TransformInputCoords(double *windowX, double *windowY);

private:
    // UI buffer management
    static bool CreateOffscreenBuffers(int width, int height);
    static void DestroyOffscreenBuffers(void);
    static void BlitToScreen(void);
    static void SetupBlitQuad(void);
    
    // 3D camera management
    static bool Create3DCamera(void);
    static void Destroy3DCamera(void);
    static void Blit3DToUI(void);
    static void Setup3DBlitQuad(void);
    
    // UI buffer rasters
    static RwRaster *ms_offscreenColor;
    static RwRaster *ms_offscreenDepth;
    
    // Original camera rasters (saved during BeginFrame)
    static RwRaster *ms_origColor;
    static RwRaster *ms_origDepth;
    
    // 3D camera (owns its rasters, immune to CameraSize)
    static RwCamera *ms_3dCamera;
    static RwFrame *ms_3dFrame;
    static RwCamera *ms_savedSceneCamera;  // Scene.camera saved during Begin3D
    
    // Cached blit geometry for 3D->UI (initialized once)
    static RwIm2DVertex ms_3dBlitVerts[4];
    static RwImVertexIndex ms_3dBlitIndices[6];
    
    // Cached blit geometry for UI->Screen
    static RwIm2DVertex ms_screenBlitVerts[4];
    static RwImVertexIndex ms_screenBlitIndices[6];
    
    // State
    static bool ms_initialized;
    static bool ms_enabled;
    static bool ms_inFrame;
    static bool ms_in3D;
    
    // Saved RsGlobal dimensions during 3D render
    static int ms_savedRsWidth;
    static int ms_savedRsHeight;

    // UI render parameters (logical size, landscape)
    static int ms_renderWidth;
    static int ms_renderHeight;
    static int ms_windowWidth;
    static int ms_windowHeight;
    static float ms_renderAspect;
    static Rotation ms_rotation;
    
    // 3D render parameters
    static int ms_3dWidth;
    static int ms_3dHeight;
    static float ms_3dScale;
};

#endif // OFFSCREEN_RENDER

#endif // OFFSCREEN_RENDERER_H