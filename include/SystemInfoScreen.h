#pragma once

#include <Adafruit_ST7789.h>

class SystemInfoScreen
{
public:
    explicit SystemInfoScreen(Adafruit_ST7789& display);

    void draw();

private:
    Adafruit_ST7789& display_;
};
