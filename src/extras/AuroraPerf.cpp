/*
 * AuroraPerf.cpp - Runtime performance settings implementation
 */

#ifdef AURORAOS

#include "common.h"
#include "AuroraPerf.h"
#include "Camera.h"
#include "Draw.h"
#include "General.h"
#include "FileMgr.h"

#include "imgui.h"

// =====================================================
// Static member initialization - Default values (Medium preset)
// =====================================================

// Effects toggles
bool CAuroraPerf::ms_bEnableSkidmarks = true;
bool CAuroraPerf::ms_bEnableCoronaReflections = true;
bool CAuroraPerf::ms_bEnableStoredShadows = true;
bool CAuroraPerf::ms_bEnableFogEffect = true;
bool CAuroraPerf::ms_bEnableGlass = true;
bool CAuroraPerf::ms_bEnableWaterCannons = true;
bool CAuroraPerf::ms_bEnableMovingThings = true;
bool CAuroraPerf::ms_bEnableRubbish = true;
bool CAuroraPerf::ms_bEnableAntennas = true;
bool CAuroraPerf::ms_bEnableRainStreaks = true;

// LOD
float CAuroraPerf::ms_fLODMultiplier = 1.0f;
float CAuroraPerf::ms_fDrawDistance = 300.0f;

// Particles
float CAuroraPerf::ms_fParticleScale = 1.0f;
float CAuroraPerf::ms_fParticleMaxDistance = 100.0f;
uint32 CAuroraPerf::ms_nEssentialParticles = 
    ESSENTIAL_FIRE | ESSENTIAL_EXPLOSION | ESSENTIAL_SMOKE_DARK | ESSENTIAL_GUNFLASH;

// Shadows
bool CAuroraPerf::ms_bSimpleShadowsOnly = false;
int CAuroraPerf::ms_nMaxShadows = 16;

// Population
float CAuroraPerf::ms_fPedDensityMult = 1.0f;
float CAuroraPerf::ms_fCarDensityMult = 1.0f;

// Coronas
float CAuroraPerf::ms_fCoronaDistance = 100.0f;
bool CAuroraPerf::ms_bCoronasEnabled = true;

// =====================================================
// Initialization
// =====================================================

void CAuroraPerf::Init(void)
{
    // Try to load settings from INI
    // LoadFromIni("aurora_settings.ini");
    
    // Apply LOD settings immediately
    ApplyLODSettings();
}

void CAuroraPerf::Shutdown(void)
{
    // Save current settings
    // SaveToIni("aurora_settings.ini");
}

// =====================================================
// Presets
// =====================================================

void CAuroraPerf::SetPresetUltraLow(void)
{
    // Disable all expensive effects
    ms_bEnableSkidmarks = false;
    ms_bEnableCoronaReflections = false;
    ms_bEnableStoredShadows = false;
    ms_bEnableFogEffect = false;
    ms_bEnableGlass = false;
    ms_bEnableWaterCannons = false;
    ms_bEnableMovingThings = false;
    ms_bEnableRubbish = false;
    ms_bEnableAntennas = false;
    ms_bEnableRainStreaks = false;
    
    // Aggressive LOD reduction
    ms_fLODMultiplier = 0.4f;
    ms_fDrawDistance = 120.0f;
    
    // Minimal particles
    ms_fParticleScale = 0.2f;
    ms_fParticleMaxDistance = 30.0f;
    
    // Minimal shadows
    ms_bSimpleShadowsOnly = true;
    ms_nMaxShadows = 2;
    
    // Low population
    ms_fPedDensityMult = 0.2f;
    ms_fCarDensityMult = 0.2f;
    
    // Coronas
    ms_fCoronaDistance = 40.0f;
    ms_bCoronasEnabled = true;  // Keep for gameplay (traffic lights etc)
    
    ApplyLODSettings();
}

void CAuroraPerf::SetPresetLow(void)
{
    // Disable most expensive effects
    ms_bEnableSkidmarks = false;
    ms_bEnableCoronaReflections = false;
    ms_bEnableStoredShadows = false;
    ms_bEnableFogEffect = false;
    ms_bEnableGlass = true;
    ms_bEnableWaterCannons = false;
    ms_bEnableMovingThings = true;
    ms_bEnableRubbish = false;
    ms_bEnableAntennas = false;
    ms_bEnableRainStreaks = true;
    
    // Reduced LOD
    ms_fLODMultiplier = 0.6f;
    ms_fDrawDistance = 200.0f;
    
    // Reduced particles
    ms_fParticleScale = 0.4f;
    ms_fParticleMaxDistance = 50.0f;
    
    // Simple shadows
    ms_bSimpleShadowsOnly = true;
    ms_nMaxShadows = 4;
    
    // Reduced population
    ms_fPedDensityMult = 0.4f;
    ms_fCarDensityMult = 0.4f;
    
    // Coronas
    ms_fCoronaDistance = 60.0f;
    ms_bCoronasEnabled = true;
    
    ApplyLODSettings();
}

void CAuroraPerf::SetPresetMedium(void)
{
    // Keep most effects, disable heaviest
    ms_bEnableSkidmarks = true;
    ms_bEnableCoronaReflections = false;  // Still expensive
    ms_bEnableStoredShadows = true;
    ms_bEnableFogEffect = false;          // Fillrate heavy
    ms_bEnableGlass = true;
    ms_bEnableWaterCannons = true;
    ms_bEnableMovingThings = true;
    ms_bEnableRubbish = true;
    ms_bEnableAntennas = true;
    ms_bEnableRainStreaks = true;
    
    // Moderate LOD
    ms_fLODMultiplier = 0.8f;
    ms_fDrawDistance = 280.0f;
    
    // Normal particles
    ms_fParticleScale = 0.7f;
    ms_fParticleMaxDistance = 80.0f;
    
    // Normal shadows
    ms_bSimpleShadowsOnly = false;
    ms_nMaxShadows = 8;
    
    // Normal population
    ms_fPedDensityMult = 0.7f;
    ms_fCarDensityMult = 0.7f;
    
    // Coronas
    ms_fCoronaDistance = 80.0f;
    ms_bCoronasEnabled = true;
    
    ApplyLODSettings();
}

void CAuroraPerf::SetPresetHigh(void)
{
    // All effects enabled
    ms_bEnableSkidmarks = true;
    ms_bEnableCoronaReflections = true;
    ms_bEnableStoredShadows = true;
    ms_bEnableFogEffect = true;
    ms_bEnableGlass = true;
    ms_bEnableWaterCannons = true;
    ms_bEnableMovingThings = true;
    ms_bEnableRubbish = true;
    ms_bEnableAntennas = true;
    ms_bEnableRainStreaks = true;
    
    // Full LOD
    ms_fLODMultiplier = 1.0f;
    ms_fDrawDistance = 350.0f;
    
    // Full particles
    ms_fParticleScale = 1.0f;
    ms_fParticleMaxDistance = 120.0f;
    
    // Full shadows
    ms_bSimpleShadowsOnly = false;
    ms_nMaxShadows = 16;
    
    // Full population
    ms_fPedDensityMult = 1.0f;
    ms_fCarDensityMult = 1.0f;
    
    // Coronas
    ms_fCoronaDistance = 100.0f;
    ms_bCoronasEnabled = true;
    
    ApplyLODSettings();
}

// =====================================================
// Settings Persistence
// =====================================================

void CAuroraPerf::LoadFromIni(char line[])
{
    // char path[256];
    // sprintf(path, "%s", filename);
    
    // FILE* f = fopen(path, "r");
    // if (!f) {
        // File doesn't exist, use defaults
        // return;
    // }
    
    // char line[256];
    char key[64];
    char value[64];
    
    // while (fgets(line, sizeof(line), f)) {
        // Skip comments and empty lines
    if (line[0] == ';' || line[0] == '#' || line[0] == '\n' || line[0] == '[')
        return;
    
    if (sscanf(line, "%63[^=]=%63s", key, value) == 2) {
        // Trim whitespace
        char* k = key;
        while (*k == ' ') k++;
        
        // Effects
        if (strcmp(k, "EnableSkidmarks") == 0)
            ms_bEnableSkidmarks = atoi(value) != 0;
        else if (strcmp(k, "EnableCoronaReflections") == 0)
            ms_bEnableCoronaReflections = atoi(value) != 0;
        else if (strcmp(k, "EnableStoredShadows") == 0)
            ms_bEnableStoredShadows = atoi(value) != 0;
        else if (strcmp(k, "EnableFogEffect") == 0)
            ms_bEnableFogEffect = atoi(value) != 0;
        else if (strcmp(k, "EnableGlass") == 0)
            ms_bEnableGlass = atoi(value) != 0;
        else if (strcmp(k, "EnableWaterCannons") == 0)
            ms_bEnableWaterCannons = atoi(value) != 0;
        else if (strcmp(k, "EnableMovingThings") == 0)
            ms_bEnableMovingThings = atoi(value) != 0;
        else if (strcmp(k, "EnableRubbish") == 0)
            ms_bEnableRubbish = atoi(value) != 0;
        else if (strcmp(k, "EnableAntennas") == 0)
            ms_bEnableAntennas = atoi(value) != 0;
        else if (strcmp(k, "EnableRainStreaks") == 0)
            ms_bEnableRainStreaks = atoi(value) != 0;
        
        // LOD
        else if (strcmp(k, "LODMultiplier") == 0)
            ms_fLODMultiplier = (float)atof(value);
        else if (strcmp(k, "DrawDistance") == 0)
            ms_fDrawDistance = (float)atof(value);
        
        // Particles
        else if (strcmp(k, "ParticleScale") == 0)
            ms_fParticleScale = (float)atof(value);
        else if (strcmp(k, "ParticleMaxDistance") == 0)
            ms_fParticleMaxDistance = (float)atof(value);
        
        // Shadows
        else if (strcmp(k, "SimpleShadowsOnly") == 0)
            ms_bSimpleShadowsOnly = atoi(value) != 0;
        else if (strcmp(k, "MaxShadows") == 0)
            ms_nMaxShadows = atoi(value);
        
        // Population
        else if (strcmp(k, "PedDensity") == 0)
            ms_fPedDensityMult = (float)atof(value);
        else if (strcmp(k, "CarDensity") == 0)
            ms_fCarDensityMult = (float)atof(value);
        
        // Coronas
        else if (strcmp(k, "CoronaDistance") == 0)
            ms_fCoronaDistance = (float)atof(value);
        else if (strcmp(k, "CoronasEnabled") == 0)
            ms_bCoronasEnabled = atoi(value) != 0;
    }
    // }
    
    // fclose(f);
}

void CAuroraPerf::SaveToIni(FILE* f)
{
    // FILE* f = fopen(filename, "w");
    // if (!f) return;
    
    fprintf(f, "; Aurora OS Performance Settings\n");
    fprintf(f, "; Generated automatically\n\n");
    
    fprintf(f, "[Effects]\n");
    fprintf(f, "EnableSkidmarks=%d\n", ms_bEnableSkidmarks ? 1 : 0);
    fprintf(f, "EnableCoronaReflections=%d\n", ms_bEnableCoronaReflections ? 1 : 0);
    fprintf(f, "EnableStoredShadows=%d\n", ms_bEnableStoredShadows ? 1 : 0);
    fprintf(f, "EnableFogEffect=%d\n", ms_bEnableFogEffect ? 1 : 0);
    fprintf(f, "EnableGlass=%d\n", ms_bEnableGlass ? 1 : 0);
    fprintf(f, "EnableWaterCannons=%d\n", ms_bEnableWaterCannons ? 1 : 0);
    fprintf(f, "EnableMovingThings=%d\n", ms_bEnableMovingThings ? 1 : 0);
    fprintf(f, "EnableRubbish=%d\n", ms_bEnableRubbish ? 1 : 0);
    fprintf(f, "EnableAntennas=%d\n", ms_bEnableAntennas ? 1 : 0);
    fprintf(f, "EnableRainStreaks=%d\n", ms_bEnableRainStreaks ? 1 : 0);
    
    fprintf(f, "\n[LOD]\n");
    fprintf(f, "LODMultiplier=%.2f\n", ms_fLODMultiplier);
    fprintf(f, "DrawDistance=%.1f\n", ms_fDrawDistance);
    
    fprintf(f, "\n[Particles]\n");
    fprintf(f, "ParticleScale=%.2f\n", ms_fParticleScale);
    fprintf(f, "ParticleMaxDistance=%.1f\n", ms_fParticleMaxDistance);
    
    fprintf(f, "\n[Shadows]\n");
    fprintf(f, "SimpleShadowsOnly=%d\n", ms_bSimpleShadowsOnly ? 1 : 0);
    fprintf(f, "MaxShadows=%d\n", ms_nMaxShadows);
    
    fprintf(f, "\n[Population]\n");
    fprintf(f, "PedDensity=%.2f\n", ms_fPedDensityMult);
    fprintf(f, "CarDensity=%.2f\n", ms_fCarDensityMult);
    
    fprintf(f, "\n[Coronas]\n");
    fprintf(f, "CoronaDistance=%.1f\n", ms_fCoronaDistance);
    fprintf(f, "CoronasEnabled=%d\n", ms_bCoronasEnabled ? 1 : 0);
    
    // fclose(f);
}

// =====================================================
// Particle Filtering
// =====================================================

bool CAuroraPerf::ShouldRenderParticle(int particleType, float distance)
{
    // Always skip if beyond max distance
    if (distance > ms_fParticleMaxDistance)
        return false;
    
    // Check if this is an essential particle type
    // (Fire, explosion, smoke from damage, gunflash)
    // These should always render regardless of scale
    
    // TODO: Map particleType to essential categories
    // For now, do random skip based on scale
    
    if (ms_fParticleScale >= 1.0f)
        return true;
    
    // Random chance based on scale
    // Scale 0.3 = 30% chance to render non-essential particles
    int threshold = (int)(ms_fParticleScale * 255.0f);
    return (CGeneral::GetRandomNumber() & 0xFF) < threshold;
}

// =====================================================
// LOD Application
// =====================================================

void CAuroraPerf::ApplyLODSettings(void)
{
    // This should be called when settings change
    // The actual LOD multiplier is used in CRenderer
    
    // Camera LOD multiplier
    TheCamera.LODDistMultiplier = ms_fLODMultiplier;
}

// =====================================================
// ImGui Panel
// =====================================================

void CAuroraPerf::RenderImGuiPanel(void)
{
    if (!ImGui::CollapsingHeader("Performance Settings"))
        return;
    
    ImGui::Indent();
    
    // Presets
    ImGui::Text("Quick Presets:");
    if (ImGui::Button("Ultra Low")) SetPresetUltraLow();
    ImGui::SameLine();
    if (ImGui::Button("Low")) SetPresetLow();
    ImGui::SameLine();
    if (ImGui::Button("Medium")) SetPresetMedium();
    ImGui::SameLine();
    if (ImGui::Button("High")) SetPresetHigh();
    
    ImGui::Separator();
    
    // Visual Effects
    if (ImGui::TreeNode("Visual Effects")) {
        ImGui::Checkbox("Skidmarks", &ms_bEnableSkidmarks);
        ImGui::Checkbox("Corona Reflections", &ms_bEnableCoronaReflections);
        ImGui::Checkbox("Stored Shadows", &ms_bEnableStoredShadows);
        ImGui::Checkbox("Volumetric Fog", &ms_bEnableFogEffect);
        ImGui::Checkbox("Glass Effects", &ms_bEnableGlass);
        ImGui::Checkbox("Water Cannons", &ms_bEnableWaterCannons);
        ImGui::Checkbox("Moving Things", &ms_bEnableMovingThings);
        ImGui::Checkbox("Rubbish/Leaves", &ms_bEnableRubbish);
        ImGui::Checkbox("Antennas", &ms_bEnableAntennas);
        ImGui::Checkbox("Rain Streaks", &ms_bEnableRainStreaks);
        ImGui::TreePop();
    }
    
    // LOD Settings
    if (ImGui::TreeNode("Draw Distance")) {
        if (ImGui::SliderFloat("LOD Multiplier", &ms_fLODMultiplier, 0.3f, 1.0f)) {
            ApplyLODSettings();
        }
        ImGui::SliderFloat("Max Distance", &ms_fDrawDistance, 100.0f, 400.0f);
        ImGui::TreePop();
    }
    
    // Particle Settings
    if (ImGui::TreeNode("Particles")) {
        ImGui::SliderFloat("Particle Scale", &ms_fParticleScale, 0.1f, 1.0f);
        ImGui::SliderFloat("Particle Distance", &ms_fParticleMaxDistance, 20.0f, 120.0f);
        ImGui::TreePop();
    }
    
    // Shadow Settings
    if (ImGui::TreeNode("Shadows")) {
        ImGui::Checkbox("Simple Shadows Only", &ms_bSimpleShadowsOnly);
        ImGui::SliderInt("Max Shadows", &ms_nMaxShadows, 1, 16);
        ImGui::TreePop();
    }
    
    // Population
    if (ImGui::TreeNode("Population")) {
        ImGui::SliderFloat("Ped Density", &ms_fPedDensityMult, 0.0f, 1.0f);
        ImGui::SliderFloat("Car Density", &ms_fCarDensityMult, 0.0f, 1.0f);
        ImGui::TreePop();
    }
    
    // Coronas
    if (ImGui::TreeNode("Coronas")) {
        ImGui::Checkbox("Enable Coronas", &ms_bCoronasEnabled);
        ImGui::SliderFloat("Corona Distance", &ms_fCoronaDistance, 20.0f, 120.0f);
        ImGui::TreePop();
    }
    
    ImGui::Separator();
    
    // if (ImGui::Button("Save Settings")) {
    //     SaveToIni("aurora_settings.ini");
    // }
    // ImGui::SameLine();
    // if (ImGui::Button("Load Settings")) {
    //     LoadFromIni("aurora_settings.ini");
    //     ApplyLODSettings();
    // }
    
    ImGui::Unindent();
}

#endif // AURORA_PERF_MODE