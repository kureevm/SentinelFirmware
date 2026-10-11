#pragma once

#include <Adafruit_ST7789.h>
#include <SentinelWiFiGuard.h>

class WiFiGuardScreen
{
public:
    WiFiGuardScreen(Adafruit_ST7789& display, SentinelWiFiGuard& guard);

    bool enter();
    void stop();

    // Returns true when the user requests a return to the main menu.
    bool update(bool previousChannel, bool back, bool nextChannel);

private:
    bool start();
    void draw();

    Adafruit_ST7789& display_;
    SentinelWiFiGuard& guard_;
    uint8_t channel_;
    uint32_t lastDrawMs_;
    uint32_t lastHopMs_;
    bool autoMode_;
};
