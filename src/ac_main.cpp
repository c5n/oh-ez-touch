#include "ac_main.hpp"
#include "ac_settings.hpp"
#include "config.hpp"

#include <WebServer.h>
#include "ota/HTTPUpdateServer.h"

#include "AutoConnect.h"

#ifndef DEBUG_AC_MAIN
#define DEBUG_AC_MAIN 0
#endif

#define MAX_HOSTNAME_LENGTH 64

#ifndef UPDATER_SERVER_PORT
#define UPDATER_SERVER_PORT 80
#endif

#ifndef UPDATER_USERNAME
#define UPDATER_USERNAME ""
#endif

#ifndef UPDATER_PASSWORD
#define UPDATER_PASSWORD ""
#endif

WebServer httpServer;
AutoConnect Portal(httpServer);
AutoConnectConfig ACConfig;
AutoConnectAux update("/update", "Update");
HTTPUpdateServer httpUpdater;

static void page_not_found_handler()
{
#if DEBUG_AC_MAIN
    Serial.println("page_not_found_handler");
#endif

    httpServer.sendHeader("Location", "/_ac", true);
    httpServer.send(302, "text/plane", "");
}

bool startCP(IPAddress ip)
{
#if DEBUG_AC_MAIN
    Serial.println("ac_main: Captive Portal started, IP:" + WiFi.localIP().toString());
#endif
    return true;
}

void ac_main_reconnect()
{
    ACConfig.autoRise = false; // do not start captive portal on reconnect
    Portal.config(ACConfig);
    Portal.begin();
}

void ac_main_setup(Config *config)
{
    ACConfig.title = "OhEzTouch";

    ACConfig.hostName = String(config->item.general.hostname);

    Serial.print("Hostname: ");
    Serial.println(ACConfig.hostName);

    httpUpdater.setup(&httpServer, UPDATER_USERNAME, UPDATER_PASSWORD);

    ACConfig.autoReconnect = true;
    ACConfig.autoReset = true;
    ACConfig.apid = ACConfig.hostName;
    ACConfig.portalTimeout = 60 * 1000; // close portal after timeout
    ACConfig.homeUri = "_ac";           // we do not have an own site. go to ac main site.
    Portal.config(ACConfig);

    ac_settings_setup(config);
    Portal.join({openhab_settings, openhab_settings_save});
    Portal.join({update});
    Portal.on("/openhab_settings", ac_settings_handler);
    Portal.on("/openhab_settings_save", ac_settings_save_handler);
    Portal.onDetect(startCP);

    Portal.begin();

    httpServer.onNotFound(page_not_found_handler);
}

void ac_main_loop()
{
    Portal.handleClient();
}
