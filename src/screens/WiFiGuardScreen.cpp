#include "WiFiGuardScreen.h"
#include "StorageFeatures.h"

WiFiGuardScreen::WiFiGuardScreen(
    Adafruit_ST7789& display,
    SentinelWiFiGuard& guard
)
    : display_(display),
      guard_(guard),
      channel_(1),
      lastDrawMs_(0),
      lastHopMs_(0),
      autoMode_(true)
{
}

bool WiFiGuardScreen::enter()
{
    autoMode_ = true;
    channel_ = 1;
    return start();
}

void WiFiGuardScreen::stop()
{
    bool wasActive = guard_.isActive();
    guard_.stop();

    if (wasActive)
        Serial.println("WiFi Guard stopped");
}

bool WiFiGuardScreen::update(
    bool previousChannel,
    bool back,
    bool nextChannel
)
{
    if (previousChannel)
    {
        autoMode_ = !autoMode_;
        lastHopMs_ = millis();
        Serial.printf(
            "WiFi Guard mode: %s\n",
            autoMode_ ? "automatic channel scan" : "manual channel"
        );
        draw();
        return false;
    }

    if (back)
    {
        stop();
        return true;
    }

    if (nextChannel)
    {
        autoMode_ = false;
        channel_ = channel_ < 13 ? channel_ + 1 : 1;
        String error;

        if (!guard_.setChannel(channel_, error))
            Serial.println(error);

        lastHopMs_ = millis();
        draw();
        return false;
    }

    if (autoMode_ && millis() - lastHopMs_ >= 700)
    {
        channel_ = channel_ < 13 ? channel_ + 1 : 1;
        String error;

        if (!guard_.setChannel(channel_, error))
            Serial.println(error);

        lastHopMs_ = millis();
    }

    if (millis() - lastDrawMs_ >= 500)
        draw();

    return false;
}

bool WiFiGuardScreen::start()
{
    stop();
    String error;

    if (!guard_.begin(channel_, error))
    {
        Serial.println(error);
        draw();
        return false;
    }

    Serial.printf("WiFi Guard started on channel %u\n", channel_);
    lastHopMs_ = millis();
    draw();
    return true;
}

void WiFiGuardScreen::draw()
{
    SentinelWiFiGuardSnapshot stats = guard_.snapshot();
    logWiFiGuardSnapshot(stats);
    const bool hasEvents =
        stats.deauthFrames > 0 || stats.disassocFrames > 0;

    display_.fillScreen(ST77XX_BLACK);
    display_.fillRect(0, 0, 320, 38, ST77XX_BLUE);
    display_.setTextColor(ST77XX_WHITE);
    display_.setTextSize(2);
    display_.setCursor(10, 11);
    display_.print("WIFI GUARD");

    display_.setTextSize(1);
    display_.setTextColor(stats.active ? ST77XX_GREEN : ST77XX_RED);
    display_.setCursor(245, 15);
    display_.print(stats.active ? "LISTEN" : "STOPPED");

    display_.setTextColor(ST77XX_GREEN);
    display_.setCursor(10, 52);
    display_.print("Channel:");
    display_.setTextColor(ST77XX_WHITE);
    display_.setCursor(105, 52);
    display_.print(channel_);
    display_.setCursor(155, 52);
    display_.setTextColor(autoMode_ ? ST77XX_GREEN : ST77XX_YELLOW);
    display_.print(autoMode_ ? "AUTO 1-13" : "MANUAL");

    display_.setTextColor(ST77XX_GREEN);
    display_.setCursor(10, 74);
    display_.print("Mgmt frames:");
    display_.setTextColor(ST77XX_WHITE);
    display_.setCursor(105, 74);
    display_.print(stats.managementFrames);

    display_.setTextColor(ST77XX_GREEN);
    display_.setCursor(10, 96);
    display_.print("Beacons:");
    display_.setTextColor(ST77XX_WHITE);
    display_.setCursor(105, 96);
    display_.print(stats.beacons);

    display_.setTextColor(ST77XX_GREEN);
    display_.setCursor(10, 118);
    display_.print("Probe req/resp:");
    display_.setTextColor(ST77XX_WHITE);
    display_.setCursor(130, 118);
    display_.print(stats.probeRequests);
    display_.print(" / ");
    display_.print(stats.probeResponses);

    display_.setTextColor(hasEvents ? ST77XX_RED : ST77XX_GREEN);
    display_.setCursor(10, 140);
    display_.print("Deauth/disassoc:");
    display_.setTextColor(ST77XX_WHITE);
    display_.setCursor(145, 140);
    display_.print(stats.deauthFrames);
    display_.print(" / ");
    display_.print(stats.disassocFrames);

    display_.setTextColor(ST77XX_GREEN);
    display_.setCursor(10, 162);
    display_.print("Last source:");
    display_.setTextColor(ST77XX_WHITE);
    display_.setCursor(105, 162);

    if (hasEvents)
    {
        char address[18];
        snprintf(
            address,
            sizeof(address),
            "%02X:%02X:%02X:%02X:%02X:%02X",
            stats.lastSource[0],
            stats.lastSource[1],
            stats.lastSource[2],
            stats.lastSource[3],
            stats.lastSource[4],
            stats.lastSource[5]
        );
        display_.print(address);

        display_.setCursor(10, 184);
        display_.print("Last RSSI/ch:");
        display_.setCursor(105, 184);
        display_.print(stats.lastRssi);
        display_.print(" dBm / ");
        display_.print(stats.lastChannel);
    }
    else
    {
        display_.print("No events");
    }

    display_.drawFastHLine(0, 216, 320, ST77XX_WHITE);
    display_.setTextColor(ST77XX_WHITE);
    display_.setCursor(8, 225);
    display_.print("AUTO/MAN");
    display_.setCursor(145, 225);
    display_.print("BACK");
    display_.setCursor(290, 225);
    display_.print("CH+");

    lastDrawMs_ = millis();
}
