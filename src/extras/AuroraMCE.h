#pragma once

#include "DBusClient.h"

// MCE (Mode Control Entity) wrapper for Aurora OS
// Handles display blanking prevention

class AuroraMCE
{
public:
    AuroraMCE();
    ~AuroraMCE();

    // Initialize and connect to system bus
    bool Init();
    void Shutdown();

    // Display blanking control
    // Call this to prevent screen from turning off
    // Automatically refreshes every 60 seconds while enabled
    void SetPreventBlanking(bool prevent);
    bool IsPreventingBlanking() const { return m_preventBlanking; }

    // Manual blanking pause request (single shot)
    bool RequestBlankingPause();

    // Must be called periodically (e.g., every frame)
    void Process();

private:
    DBusClient m_dbus;
    bool m_initialized;
    bool m_preventBlanking;
    uint32_t m_blankingTimerId;

    static constexpr const char* MCE_SERVICE = "com.nokia.mce";
    static constexpr const char* MCE_REQUEST_PATH = "/com/nokia/mce/request";
    static constexpr const char* MCE_REQUEST_IF = "com.nokia.mce.request";
    
    static constexpr uint32_t BLANKING_PAUSE_INTERVAL_MS = 60000; // 60 seconds

    void OnBlankingTimer();
};