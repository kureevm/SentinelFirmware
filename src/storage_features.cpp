#include "FirmwareApp.h"
#include "StorageFeatures.h"

#include <SD.h>
#include <SPI.h>
#include <SentinelWiFiProfiles.h>

#if __has_include("wifi_credentials.h")
#include "wifi_credentials.h"
#endif

#ifndef SENTINEL_WIFI_SSID
#define SENTINEL_WIFI_SSID ""
#endif

#ifndef SENTINEL_WIFI_PASSWORD
#define SENTINEL_WIFI_PASSWORD ""
#endif

namespace
{
constexpr uint32_t SD_SPI_FREQUENCY = 1000000;
constexpr const char* SENTINEL_DIRECTORY = "/sentinel";
constexpr const char* PROFILE_EXPORT_PATH =
    "/sentinel/confirmed_wifi_credentials.csv";
constexpr const char* GUARD_LOG_PATH = "/sentinel/wifi_guard.csv";

SentinelWiFiProfiles profiles;
bool sdMounted = false;
String sdStatus = "Not initialized";
uint32_t lastLoggedDeauth = 0;
uint32_t lastLoggedDisassoc = 0;

String cardTypeName()
{
    switch (SD.cardType())
    {
        case CARD_MMC:
            return "MMC";

        case CARD_SD:
            return "SDSC";

        case CARD_SDHC:
            return "SDHC/SDXC";

        case CARD_NONE:
            return "No card";

        default:
            return "Unknown";
    }
}

String csvField(const String& value)
{
    String escaped = "\"";

    for (size_t i = 0; i < value.length(); i++)
    {
        if (value[i] == '"')
            escaped += "\"\"";
        else if (value[i] != '\r' && value[i] != '\n')
            escaped += value[i];
    }

    escaped += '"';
    return escaped;
}

void prepareSharedSPI()
{
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    pinMode(RFID_SS, OUTPUT);
    digitalWrite(RFID_SS, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    SPI.begin(SCK, MISO, MOSI);
}

bool mountSDCard()
{
    if (sdMounted)
        SD.end();

    sdMounted = false;
    prepareSharedSPI();

    if (!SD.begin(SD_CS, SPI, SD_SPI_FREQUENCY))
    {
        sdStatus = "Check wiring or FAT32";
        Serial.println(
            "SD mount failed: verify SCK=12 MISO=13 MOSI=11 CS=15, then FAT32"
        );
        return false;
    }

    if (SD.cardType() == CARD_NONE)
    {
        sdStatus = "No card detected";
        SD.end();
        Serial.println("SD card not detected");
        return false;
    }

    sdMounted = true;
    sdStatus = "Mounted";
    SD.mkdir(SENTINEL_DIRECTORY);
    Serial.printf(
        "SD mounted: %s, %llu MB\n",
        cardTypeName().c_str(),
        SD.cardSize() / (1024ULL * 1024ULL)
    );
    return true;
}

bool exportConfirmedProfiles()
{
    if (!sdMounted)
    {
        sdStatus = "Mount card first";
        return false;
    }

    SD.mkdir(SENTINEL_DIRECTORY);
    if (SD.exists(PROFILE_EXPORT_PATH))
        SD.remove(PROFILE_EXPORT_PATH);

    File file = SD.open(PROFILE_EXPORT_PATH, FILE_WRITE);

    if (!file)
    {
        sdStatus = "Cannot create profile CSV";
        return false;
    }

    file.println("ssid,password,source");
    size_t exported = 0;

    for (size_t index = 0; index < SentinelWiFiProfiles::MAX_PROFILES; index++)
    {
        SentinelWiFiProfile profile;

        if (!profiles.get(index, profile))
            continue;

        file.print(csvField(profile.ssid));
        file.print(',');
        file.print(csvField(profile.password));
        file.println(",\"sentinel_nvs\"");
        exported++;
    }

    const String buildSsid = SENTINEL_WIFI_SSID;
    if (buildSsid.length() > 0 && !profiles.contains(buildSsid))
    {
        file.print(csvField(buildSsid));
        file.print(',');
        file.print(csvField(String(SENTINEL_WIFI_PASSWORD)));
        file.println(",\"build_header\"");
        exported++;
    }

    file.close();
    sdStatus = String("Exported profiles: ") + exported;
    Serial.printf(
        "Exported %u saved WiFi profile(s) to %s; passwords not printed\n",
        static_cast<unsigned>(exported),
        PROFILE_EXPORT_PATH
    );
    return true;
}

size_t exportableProfileCount()
{
    size_t result = profiles.count();
    const String buildSsid = SENTINEL_WIFI_SSID;

    if (buildSsid.length() > 0 && !profiles.contains(buildSsid))
        result++;

    return result;
}

void drawSDCardScreen()
{
    display.fillScreen(ST77XX_BLACK);
    display.fillRect(0, 0, 320, 38, ST77XX_BLUE);
    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(2);
    display.setCursor(10, 11);
    display.print("SD CARD");
    display.setTextSize(1);

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 55);
    display.print("Status:");
    display.setTextColor(sdMounted ? ST77XX_GREEN : ST77XX_RED);
    display.setCursor(90, 55);
    display.print(sdStatus);

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 80);
    display.print("Card type:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(90, 80);
    display.print(sdMounted ? cardTypeName() : "N/A");

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 105);
    display.print("Capacity:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(90, 105);
    if (sdMounted)
    {
        display.print(SD.cardSize() / (1024ULL * 1024ULL));
        display.print(" MB");
    }
    else
    {
        display.print("N/A");
    }

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 130);
    display.print("Saved profiles:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(130, 130);
    display.print(exportableProfileCount());

    display.setTextColor(ST77XX_WHITE);
    display.setCursor(10, 160);
    display.print("EXPORT writes plaintext CSV to:");
    display.setCursor(10, 178);
    display.print(PROFILE_EXPORT_PATH);

    display.setTextColor(ST77XX_YELLOW);
    display.setCursor(10, 198);
    display.print("Remove card when device is off");

    display.drawFastHLine(0, 216, 320, ST77XX_WHITE);
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(8, 225);
    display.print("REMOUNT");
    display.setCursor(145, 225);
    display.print("BACK");
    display.setCursor(275, 225);
    display.print("EXPORT");
}

String macToString(const uint8_t* address)
{
    char text[18];
    snprintf(
        text,
        sizeof(text),
        "%02X:%02X:%02X:%02X:%02X:%02X",
        address[0],
        address[1],
        address[2],
        address[3],
        address[4],
        address[5]
    );
    return text;
}
}

bool isSDCardMounted()
{
    return sdMounted;
}

void logWiFiGuardSnapshot(const SentinelWiFiGuardSnapshot& snapshot)
{
    if (!sdMounted)
        return;

    if (snapshot.deauthFrames == lastLoggedDeauth &&
        snapshot.disassocFrames == lastLoggedDisassoc)
    {
        return;
    }

    SD.mkdir(SENTINEL_DIRECTORY);
    const bool newFile = !SD.exists(GUARD_LOG_PATH);
    File file = SD.open(GUARD_LOG_PATH, FILE_APPEND);

    if (!file)
    {
        Serial.println("Cannot append WiFi Guard event to SD");
        return;
    }

    if (newFile)
        file.println("millis,channel,deauth,disassoc,last_source,last_rssi");

    file.print(millis());
    file.print(',');
    file.print(snapshot.lastChannel);
    file.print(',');
    file.print(snapshot.deauthFrames);
    file.print(',');
    file.print(snapshot.disassocFrames);
    file.print(',');
    file.print(macToString(snapshot.lastSource));
    file.print(',');
    file.println(snapshot.lastRssi);
    file.close();

    lastLoggedDeauth = snapshot.deauthFrames;
    lastLoggedDisassoc = snapshot.disassocFrames;
}

void enterSDCard()
{
    mountSDCard();
    drawSDCardScreen();
}

void handleSDCard()
{
    if (pressed(BTN_TOP, lastTop))
    {
        mountSDCard();
        drawSDCardScreen();
        return;
    }

    if (pressed(BTN_MIDDLE, lastMiddle))
    {
        currentScreen = Screen::MainMenu;
        drawMenu();
        return;
    }

    if (pressed(BTN_BOTTOM, lastBottom))
    {
        exportConfirmedProfiles();
        drawSDCardScreen();
    }
}
