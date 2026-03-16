#include "AuroraMCE.h"
#include <cstdio>

AuroraMCE::AuroraMCE()
    : m_initialized(false)
    , m_preventBlanking(false)
    , m_blankingTimerId(0)
{
}

AuroraMCE::~AuroraMCE()
{
    Shutdown();
}

bool AuroraMCE::Init()
{
    if (m_initialized) return true;

    if (!m_dbus.Connect(DBusClient::SystemBus)) {
        fprintf(stderr, "AuroraMCE: Failed to connect to system bus\n");
        return false;
    }

    m_initialized = true;
    fprintf(stdout, "AuroraMCE: Initialized\n");
    return true;
}

void AuroraMCE::Shutdown()
{
    if (!m_initialized) return;

    SetPreventBlanking(false);
    m_dbus.Disconnect();
    m_initialized = false;

    fprintf(stdout, "AuroraMCE: Shutdown\n");
}

bool AuroraMCE::RequestBlankingPause()
{
    if (!m_initialized) return false;

    DBusResult result = m_dbus.Call(
        MCE_SERVICE,
        MCE_REQUEST_PATH,
        MCE_REQUEST_IF,
        "req_display_blanking_pause"
    );

    if (!result.IsSuccess()) {
        fprintf(stderr, "AuroraMCE: req_display_blanking_pause failed: %s\n", 
                result.GetError().c_str());
        return false;
    }

    return true;
}

void AuroraMCE::OnBlankingTimer()
{
    if (m_preventBlanking) {
        RequestBlankingPause();
    }
}

void AuroraMCE::SetPreventBlanking(bool prevent)
{
    if (m_preventBlanking == prevent) return;

    m_preventBlanking = prevent;

    if (prevent) {
        // Request immediately
        RequestBlankingPause();

        // Setup recurring timer
        m_blankingTimerId = m_dbus.AddTimer(
            BLANKING_PAUSE_INTERVAL_MS,
            [this]() { OnBlankingTimer(); },
            true
        );

        fprintf(stdout, "AuroraMCE: Blanking prevention enabled\n");
    } else {
        // Remove timer
        if (m_blankingTimerId != 0) {
            m_dbus.RemoveTimer(m_blankingTimerId);
            m_blankingTimerId = 0;
        }

        fprintf(stdout, "AuroraMCE: Blanking prevention disabled\n");
    }
}

void AuroraMCE::Process()
{
    if (!m_initialized) return;
    m_dbus.Process();
}