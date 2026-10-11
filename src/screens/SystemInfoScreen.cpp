#include "SystemInfoScreen.h"

SystemInfoScreen::SystemInfoScreen(Adafruit_ST7789& display)
    : display_(display)
{
}

void SystemInfoScreen::draw()
{
    display_.fillScreen(ST77XX_BLACK);
    display_.fillRect(0, 0, 320, 38, ST77XX_BLUE);
    display_.setTextColor(ST77XX_WHITE);
    display_.setTextSize(2);
    display_.setCursor(10, 11);
    display_.print("SYSTEM INFO");
    display_.setTextSize(1);

    int y = 55;

    auto drawValue = [&](const char* label, const String& value)
    {
        display_.setTextColor(ST77XX_GREEN);
        display_.setCursor(10, y);
        display_.print(label);
        display_.setTextColor(ST77XX_WHITE);
        display_.setCursor(105, y);
        display_.print(value);
        y += 22;
    };

    drawValue("Chip:", ESP.getChipModel());
    drawValue(
        "CPU:",
        String(ESP.getCpuFreqMHz()) + " MHz / " +
            ESP.getChipCores() + " cores"
    );
    drawValue("Flash:", String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB");
    drawValue("PSRAM:", String(ESP.getPsramSize() / 1024) + " KB");
    drawValue("Free heap:", String(ESP.getFreeHeap() / 1024) + " KB");
    drawValue("Min heap:", String(ESP.getMinFreeHeap() / 1024) + " KB");
    drawValue("Uptime:", String(millis() / 1000) + " sec");

    display_.drawFastHLine(0, 216, 320, ST77XX_WHITE);
    display_.setTextColor(ST77XX_WHITE);
    display_.setCursor(8, 225);
    display_.print("REFRESH");
    display_.setCursor(145, 225);
    display_.print("BACK");
}
