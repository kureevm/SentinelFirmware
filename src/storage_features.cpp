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
constexpr const char* RFID_LOG_PATH = "/sentinel/rfid_reads.csv";
constexpr uint32_t FORMAT_HOLD_MS = 3000;

SentinelWiFiProfiles profiles;
bool sdMounted = false;
String sdStatus = "Not initialized";
uint32_t lastLoggedDeauth = 0;
uint32_t lastLoggedDisassoc = 0;
uint32_t formatHoldStartedAt = 0;
bool formatTriggered = false;

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
        sdStatus = "No SPI response or non-FAT";
        Serial.println(
            "SD mount failed: inspect preceding sd_diskio message; "
            "verify SCK=12 MISO=13 MOSI=11 CS=15 or format as FAT32"
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

bool formatAndMountSDCard()
{
    if (sdMounted)
    {
        sdStatus = "Already mounted; format blocked";
        return false;
    }

    SD.end();
    prepareSharedSPI();
    Serial.println("SD format physically confirmed: attempting FAT/FAT32 format");

    // The final true asks the ESP32 FatFs driver to format only after the
    // card answers SPI but mounting reports that no supported filesystem
    // exists. It cannot turn a missing or incorrectly wired card into a
    // successful mount.
    if (!SD.begin(SD_CS, SPI, SD_SPI_FREQUENCY, "/sd", 5, true))
    {
        sdStatus = "Format/init failed; check wiring";
        Serial.println("SD format failed: card did not initialize or format");
        return false;
    }

    if (SD.cardType() == CARD_NONE)
    {
        sdStatus = "No card detected";
        SD.end();
        return false;
    }

    sdMounted = true;
    SD.mkdir(SENTINEL_DIRECTORY);
    sdStatus = "Formatted FAT + mounted";
    Serial.printf(
        "SD formatted and mounted: %s, %llu MB\n",
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
    display.setCursor(10, 154);
    if (sdMounted)
    {
        display.print("EXPORT: plaintext WiFi CSV");
        display.setCursor(10, 172);
        display.print("RFID log: ");
        display.print(RFID_LOG_PATH);
        display.setTextColor(ST77XX_YELLOW);
        display.setCursor(10, 198);
        display.print("Remove card when device is off");
    }
    else
    {
        display.setTextColor(ST77XX_YELLOW);
        display.print("FORMAT ERASES THE CARD");
        display.setCursor(10, 174);
        display.print("Hold REMOUNT + EXPORT for 3 sec");
        display.setCursor(10, 194);
        display.print("Use only on your own SD card");
    }

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

void initializeStorage()
{
    mountSDCard();
}

bool logRFIDRead(const String& uid, const String& type)
{
    if (!sdMounted)
        return false;

    SD.mkdir(SENTINEL_DIRECTORY);
    const bool newFile = !SD.exists(RFID_LOG_PATH);
    File file = SD.open(RFID_LOG_PATH, FILE_APPEND);

    if (!file)
    {
        Serial.println("Cannot append RFID read to SD");
        return false;
    }

    if (newFile)
        file.println("uptime_ms,uid,type");

    file.print(millis());
    file.print(',');
    file.print(csvField(uid));
    file.print(',');
    file.println(csvField(type));
    file.close();
    Serial.println("RFID read appended to SD; UID not repeated in this message");
    return true;
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
    formatHoldStartedAt = 0;
    formatTriggered = false;
    drawSDCardScreen();
}

void handleSDCard()
{
    const bool topHeld = digitalRead(BTN_TOP) == LOW;
    const bool bottomHeld = digitalRead(BTN_BOTTOM) == LOW;

    if (!sdMounted && topHeld && bottomHeld)
    {
        // Keep the normal edge detector synchronized while the combination
        // is held so release cannot accidentally trigger REMOUNT or EXPORT.
        lastTop = LOW;
        lastBottom = LOW;

        if (formatHoldStartedAt == 0)
        {
            formatHoldStartedAt = millis();
            sdStatus = "Hold both buttons: 3 sec";
            drawSDCardScreen();
        }

        if (!formatTriggered &&
            millis() - formatHoldStartedAt >= FORMAT_HOLD_MS)
        {
            formatTriggered = true;
            sdStatus = "Formatting...";
            drawSDCardScreen();
            formatAndMountSDCard();
            drawSDCardScreen();
        }

        return;
    }

    if (!topHeld || !bottomHeld)
    {
        formatHoldStartedAt = 0;
        formatTriggered = false;
    }

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
