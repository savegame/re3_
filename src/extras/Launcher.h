#pragma once

#include <string>
#include <vector>

// Minimal launcher for resource checking and path selection
// Uses std::string throughout - no fixed char buffers

class Launcher
{
public:
    enum class Result {
        Continue,   // Resources found, proceed to game
        Exit        // User requested exit
    };

    // Main entry point - call before RW initialization
    // Returns path to game resources, or empty string if user exits
    static Result Run();

    // Check if required files exist at given path
    static bool CheckResources(const std::string &basePath);

    // Get default game path
    static const std::string& GetGamePath() { return ms_gamePath; }
    static std::string GetDefaultPath();

    // Get/Set stored path (from config file)
    static std::string LoadStoredPath();
    static void SaveStoredPath(const std::string &path);

private:
    // Files required for game to run
    static const std::vector<std::string>& GetRequiredFiles();  
    static std::string ms_gamePath;
    static unsigned int ms_backgroundTexture;
    static int ms_backgroundWidth;
    static int ms_backgroundHeight;
    static bool ms_backgroundLoaded;
    // Disclaimer
    static bool ms_showDisclaimer;
    static bool ms_disclaimerAccepted;
    
    
    // Config file path
    static std::string GetConfigPath();
    static void DrawBackground(int width, int height);
    static void DrawDisclaimer(float dpiScale);
    static float CalculateDpiScale(GLFWwindow *window);
    static bool LoadBackgroundTexture(const char *path);
};