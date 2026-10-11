#pragma once

#include <Arduino.h>
#include <Adafruit_ST7789.h>

constexpr uint8_t TFT_CS = 10;
constexpr uint8_t TFT_RST = 9;
constexpr uint8_t TFT_DC = 8;

constexpr uint8_t BTN_TOP = 4;
constexpr uint8_t BTN_MIDDLE = 5;
constexpr uint8_t BTN_BOTTOM = 6;

constexpr uint8_t RFID_SS = 7;
constexpr uint8_t RFID_RST = 14;
constexpr uint8_t SD_CS = 15;

constexpr int MENU_COUNT = 7;

enum class Screen
{
    MainMenu,
    Bluetooth,
    BLEDetails,
    RFID,
    SDCard,
    WiFi,
    WiFiDetails,
    WiFiGuard,
    SystemInfo,
    WiFiCSI
};

extern Adafruit_ST7789 display;
extern const char* menuItems[MENU_COUNT];
extern int selectedItem;
extern bool lastTop;
extern bool lastMiddle;
extern bool lastBottom;
extern Screen currentScreen;

bool pressed(int pin, bool& lastState);
bool handleGlobalNavigation();
void drawMenu();
String getSignalQuality(int rssi);

void initializeWiFiServices();
void pollWiFiServices();
void reportWiFiConfiguration();
void scanWiFi();
void handleWiFi();
void handleWiFiDetails();
void enterWiFiGuard();
void handleWiFiGuardScreen();
void stopWiFiGuard();
bool startWiFiCSI();
void stopWiFiCSI();
void handleWiFiCSI();

void scanBLE();
void handleBluetooth();
void handleBLEDetails();

void enterRFID();
void handleRFID();

void enterSDCard();
void handleSDCard();

void showSystemInfo();
void handleSystemInfoScreen();
