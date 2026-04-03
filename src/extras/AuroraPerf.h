/*
 * AuroraPerf.h - Runtime performance settings for Aurora OS
 *
 * Allows toggling expensive visual effects at runtime
 * for weak mobile devices.
 *
 */

#ifndef AURORA_PERF_H
#define AURORA_PERF_H

#ifdef AURORAOS

#include "common.h"

// Essential particle types that should never be skipped
enum eEssentialParticles {
    // Fire and explosions - critical for gameplay
    ESSENTIAL_FIRE = (1 << 0),
    ESSENTIAL_EXPLOSION = (1 << 1),
    ESSENTIAL_SMOKE_DARK = (1 << 2),  // Vehicle damage smoke
    
    // Muzzle flash - important feedback
    ESSENTIAL_GUNFLASH = (1 << 3),
    
    // Water splashes - gameplay feedback
    ESSENTIAL_WATER = (1 << 4),
};

class CAuroraPerf
{
public:
    // =====================================================
    // Visual Effects Toggles
    // =====================================================
    
    // Expensive effects - disable on weak devices
    static bool ms_bEnableSkidmarks;          // Tire marks on road
    static bool ms_bEnableCoronaReflections;  // Light reflections on wet roads
    static bool ms_bEnableStoredShadows;      // Dynamic shadows for all objects
    static bool ms_bEnableFogEffect;          // Volumetric fog from lights
    
    // Medium cost effects
    static bool ms_bEnableGlass;              // Breaking glass particles
    static bool ms_bEnableWaterCannons;       // Fire truck water
    static bool ms_bEnableMovingThings;       // Escalators, etc.
    
    // Low cost effects (can usually stay on)
    static bool ms_bEnableRubbish;            // Flying leaves/paper
    static bool ms_bEnableAntennas;           // Car antenna movement
    static bool ms_bEnableRainStreaks;        // Rain visual effect
    
    // =====================================================
    // LOD and Draw Distance
    // =====================================================
    
    static float ms_fLODMultiplier;           // 0.3 - 1.0 (default 1.0)
    static float ms_fDrawDistance;            // 100 - 500 (default ~300)
    
    // =====================================================
    // Particle System
    // =====================================================
    
    static float ms_fParticleScale;           // 0.1 - 1.0 (particle count mult)
    static float ms_fParticleMaxDistance;     // Max distance for particles
    static uint32 ms_nEssentialParticles;     // Bitmask of never-skip particles
    
    // =====================================================
    // Shadow System
    // =====================================================
    
    static bool ms_bSimpleShadowsOnly;        // Only player/vehicle shadows
    static int ms_nMaxShadows;                // Limit concurrent shadows
    
    // =====================================================
    // Population
    // =====================================================
    
    static float ms_fPedDensityMult;          // 0.0 - 1.0
    static float ms_fCarDensityMult;          // 0.0 - 1.0
    
    // =====================================================
    // Corona System  
    // =====================================================
    
    static float ms_fCoronaDistance;          // Max corona render distance
    static bool ms_bCoronasEnabled;           // Master toggle
    
    // =====================================================
    // Methods
    // =====================================================
    
    static void Init(void);
    static void Shutdown(void);
    
    // Preset configurations
    static void SetPresetUltraLow(void);
    static void SetPresetLow(void);
    static void SetPresetMedium(void);
    static void SetPresetHigh(void);
    
    // Settings persistence
    static void LoadFromIni(char* l);
    static void SaveToIni  (FILE* f);
    
    // Particle filtering
    static bool ShouldRenderParticle(int particleType, float distance);
    
    // Apply LOD settings to camera
    static void ApplyLODSettings(void);

    // ImGui settings panel
    static void RenderImGuiPanel(void);
};

// =====================================================
// Inline helpers for checking effect states
// =====================================================

inline bool AuroraCanRenderSkidmarks(void) {
    return CAuroraPerf::ms_bEnableSkidmarks;
}

inline bool AuroraCanRenderCoronaReflections(void) {
    return CAuroraPerf::ms_bEnableCoronaReflections;
}

inline bool AuroraCanRenderStoredShadows(void) {
    return CAuroraPerf::ms_bEnableStoredShadows;
}

inline bool AuroraCanRenderFogEffect(void) {
    return CAuroraPerf::ms_bEnableFogEffect;
}

inline bool AuroraCanRenderGlass(void) {
    return CAuroraPerf::ms_bEnableGlass;
}

inline bool AuroraCanRenderRubbish(void) {
    return CAuroraPerf::ms_bEnableRubbish;
}

inline bool AuroraCanRenderRainStreaks(void) {
    return CAuroraPerf::ms_bEnableRainStreaks;
}

#else
// Stubs when AURORA_PERF_MODE is not defined

inline bool AuroraCanRenderSkidmarks(void) { return true; }
inline bool AuroraCanRenderCoronaReflections(void) { return true; }
inline bool AuroraCanRenderStoredShadows(void) { return true; }
inline bool AuroraCanRenderFogEffect(void) { return true; }
inline bool AuroraCanRenderGlass(void) { return true; }
inline bool AuroraCanRenderRubbish(void) { return true; }
inline bool AuroraCanRenderRainStreaks(void) { return true; }

#endif // AURORA_PERF_MODE

#endif // AURORA_PERF_H