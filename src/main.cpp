#include "FirmwareApp.h"
#include "StorageFeatures.h"

#include <NimBLEDevice.h>

Adafruit_ST7789 display(TFT_CS, TFT_DC, TFT_RST);

const char* menuItems[MENU_COUNT] = {
    "Bluetooth",
    "RFID Reader",
    "WiFi Scan",
    "WiFi Guard",
    "WiFi CSI",
    "SD Card",
    "System Info"
};

int selectedItem = 0;
bool lastTop = HIGH;
bool lastMiddle = HIGH;
bool lastBottom = HIGH;
Screen currentScreen = Screen::MainMenu;

void setup()
{
    Serial.begin(115200);
    delay(1000);
    initializeWiFiServices();

    Serial.println();
    Serial.println("SENTINEL START");

    pinMode(BTN_TOP, INPUT_PULLUP);
    pinMode(BTN_MIDDLE, INPUT_PULLUP);
    pinMode(BTN_BOTTOM, INPUT_PULLUP);

    display.init(240, 320);
    display.setRotation(1);

    // Mount once during boot so background features can log without first
    // opening the SD screen. A failed mount remains non-destructive.
    initializeStorage();

    NimBLEDevice::init("Sentinel");
    Serial.println("BLE initialized");

    reportWiFiConfiguration();
    drawMenu();
    Serial.println("READY");
}

void handleMainMenu()
{
    if (pressed(BTN_TOP, lastTop))
    {
        selectedItem--;

        if (selectedItem < 0)
            selectedItem = MENU_COUNT - 1;

        drawMenu();
    }

    if (pressed(BTN_BOTTOM, lastBottom))
    {
        selectedItem++;

        if (selectedItem >= MENU_COUNT)
            selectedItem = 0;

        drawMenu();
    }

    if (!pressed(BTN_MIDDLE, lastMiddle))
        return;

    switch (selectedItem)
    {
        case 0:
            currentScreen = Screen::Bluetooth;
            scanBLE();
            break;

        case 1:
            currentScreen = Screen::RFID;
            enterRFID();
            break;

        case 2:
            currentScreen = Screen::WiFi;
            scanWiFi();
            break;

        case 3:
            currentScreen = Screen::WiFiGuard;
            enterWiFiGuard();
            break;

        case 4:
            currentScreen = Screen::WiFiCSI;
            startWiFiCSI();
            break;

        case 5:
            currentScreen = Screen::SDCard;
            enterSDCard();
            break;

        case 6:
            currentScreen = Screen::SystemInfo;
            showSystemInfo();
            break;

        default:
            Serial.print("Not implemented: ");
            Serial.println(menuItems[selectedItem]);
            break;
    }
}

void loop()
{
    pollWiFiServices();

    if (handleGlobalNavigation())
    {
        delay(20);
        return;
    }

    switch (currentScreen)
    {
        case Screen::MainMenu:
            handleMainMenu();
            break;

        case Screen::Bluetooth:
            handleBluetooth();
            break;

        case Screen::BLEDetails:
            handleBLEDetails();
            break;

        case Screen::RFID:
            handleRFID();
            break;

        case Screen::SDCard:
            handleSDCard();
            break;

        case Screen::WiFi:
            handleWiFi();
            break;

        case Screen::WiFiDetails:
            handleWiFiDetails();
            break;

        case Screen::WiFiGuard:
            handleWiFiGuardScreen();
            break;

        case Screen::SystemInfo:
            handleSystemInfoScreen();
            break;

        case Screen::WiFiCSI:
            handleWiFiCSI();
            break;
    }

    delay(20);
}
