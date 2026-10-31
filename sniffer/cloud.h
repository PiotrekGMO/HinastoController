// Client of the Hinasto server: keeps WiFi up, syncs state/commands/log with POST <api>/api/v1/sync, serves OTA.
#pragma once
#include <Arduino.h>
#include "settings.h"

enum CloudState : uint8_t { CS_WIFI_DOWN, CS_NO_SERVER, CS_PENDING, CS_REJECTED, CS_ONLINE };

void cloudSetup(const Settings& s);  // joins WiFi, starts the sync task
void cloudLoop();                    // call from loop(): OTA and WiFi supervision
CloudState cloudState();
bool cloudWifiLostLong();            // no WiFi for minutes: the caller may open the setup portal

// While the setup portal tests new settings the sync task must stay away from WiFi.
void cloudHold(bool hold);

void cloudUsePsram();                // TLS buffers into the PSRAM (called before the first TLS connection)
bool cloudSyncTime(uint32_t ms);     // wait for NTP (certificate validity); true when the clock is set
bool cloudTimeFromServer(const Settings& s);  // fallback: the clock from the Date header of the server

// One registration request with the given settings (WiFi must be up). Returns the HTTP status, or <0 on a
// transport error; message gets a short explanation in Polish for the portal.
int cloudProbe(const Settings& s, String& message);
