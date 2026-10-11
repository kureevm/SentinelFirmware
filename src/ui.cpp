#include "FirmwareApp.h"

#include <SystemInfoScreen.h>

namespace
{
SystemInfoScreen systemInfoScreen(display);
}

// ================= BUTTON =================

bool pressed(int pin, bool& lastState)
{
    bool currentState = digitalRead(pin);

    bool event =
        lastState == HIGH &&
        currentState == LOW;

    lastState = currentState;

    return event;
}

bool handleGlobalNavigation()
{
    constexpr uint32_t HOME_HOLD_MS = 900;
    static uint32_t middlePressedAt = 0;
    static bool homeTriggered = false;

    if (currentScreen == Screen::MainMenu)
    {
        middlePressedAt = 0;
        homeTriggered = false;
        return false;
    }

    if (digitalRead(BTN_MIDDLE) == HIGH)
    {
        middlePressedAt = 0;
        homeTriggered = false;
        return false;
    }

    if (middlePressedAt == 0)
    {
        middlePressedAt = millis();
        return false;
    }

    if (homeTriggered || millis() - middlePressedAt < HOME_HOLD_MS)
        return false;

    stopWiFiGuard();
    stopWiFiCSI();
    homeTriggered = true;
    currentScreen = Screen::MainMenu;
    drawMenu();
    Serial.println("Navigation: returned to main menu");
    return true;
}

// ================= MAIN MENU =================

void drawMenu()
{
    display.fillScreen(ST77XX_BLACK);

    display.fillRect(
        0,
        0,
        320,
        38,
        ST77XX_BLUE
    );

    display.setTextSize(2);
    display.setTextColor(ST77XX_WHITE);

    display.setCursor(12, 11);
    display.print("SENTINEL");

    for (int i = 0; i < MENU_COUNT; i++)
    {
        int y = 45 + i * 25;

        if (i == selectedItem)
        {
            display.fillRoundRect(
                8,
                y - 5,
                304,
                23,
                5,
                ST77XX_WHITE
            );

            display.setTextColor(ST77XX_BLACK);
        }
        else
        {
            display.setTextColor(ST77XX_WHITE);
        }

        display.setCursor(20, y);
        display.print(menuItems[i]);
    }

    display.setTextSize(1);
    display.setTextColor(ST77XX_WHITE);

    display.setCursor(8, 225);
    display.print("UP");

    display.setCursor(148, 225);
    display.print("OK");

    display.setCursor(280, 225);
    display.print("DOWN");
}

String getSignalQuality(int rssi)
{
    if (rssi >= -50)
        return "Excellent";

    if (rssi >= -65)
        return "Good";

    if (rssi >= -80)
        return "Weak";

    return "Very weak";
}

void showSystemInfo()
{
    systemInfoScreen.draw();
}

void handleSystemInfoScreen()
{
    if (pressed(BTN_TOP, lastTop))
        systemInfoScreen.draw();

    if (pressed(BTN_MIDDLE, lastMiddle) ||
        pressed(BTN_BOTTOM, lastBottom))
    {
        currentScreen = Screen::MainMenu;
        drawMenu();
    }
}
