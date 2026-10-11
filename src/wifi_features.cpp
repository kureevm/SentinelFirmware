#include "FirmwareApp.h"

#include <SentinelWiFiGuard.h>
#include <SentinelWiFiProfileConsole.h>
#include <SentinelWiFiProfiles.h>
#include <WiFi.h>
#include <WiFiGuardScreen.h>
#include <esp_wifi.h>

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
constexpr const char* WIFI_SSID = SENTINEL_WIFI_SSID;
constexpr const char* WIFI_PASSWORD = SENTINEL_WIFI_PASSWORD;
constexpr int MAX_WIFI_NETWORKS = 30;

struct WiFiNetworkInfo
{
    String ssid;
    String bssid;
    int rssi;
    int channel;
    String security;
    bool savedProfile;
};

WiFiNetworkInfo wifiNetworks[MAX_WIFI_NETWORKS];
int wifiNetworkCount = 0;
int wifiSelected = 0;

SentinelWiFiGuard wifiGuard;
SentinelWiFiProfiles wifiProfiles;
SentinelWiFiProfileConsole wifiProfileConsole(wifiProfiles, Serial);
WiFiGuardScreen wifiGuardScreen(display, wifiGuard);

portMUX_TYPE csiStatsMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t csiPacketCount = 0;
volatile int32_t csiLastRssi = 0;
volatile uint32_t csiMeanMagnitude = 0;
volatile uint32_t csiMagnitudeDelta = 0;
bool csiActive = false;
uint32_t csiLastDrawMs = 0;
uint32_t csiRateReferenceMs = 0;
uint32_t csiRateReferencePackets = 0;
uint32_t csiPacketsPerSecond = 0;
}

String getWiFiSecurity(wifi_auth_mode_t encryption)
{
    switch (encryption)
    {
        case WIFI_AUTH_OPEN:
            return "Open";

        case WIFI_AUTH_WEP:
            return "WEP";

        case WIFI_AUTH_WPA_PSK:
            return "WPA";

        case WIFI_AUTH_WPA2_PSK:
            return "WPA2";

        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA/WPA2";

        case WIFI_AUTH_WPA2_ENTERPRISE:
            return "WPA2 Enterprise";

        case WIFI_AUTH_WPA3_PSK:
            return "WPA3";

        case WIFI_AUTH_WPA2_WPA3_PSK:
            return "WPA2/WPA3";

        default:
            return "Unknown";
    }
}

void stopWiFiGuard()
{
    wifiGuardScreen.stop();
}

void drawWiFiScanning()
{
    display.fillScreen(ST77XX_BLACK);

    display.fillRect(
        0,
        0,
        320,
        38,
        ST77XX_BLUE
    );

    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(2);

    display.setCursor(12, 11);
    display.print("WIFI SCANNER");

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(75, 95);
    display.print("Scanning...");

    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(1);

    display.setCursor(75, 140);
    display.print("Searching networks");
}

void drawWiFiResults()
{
    display.fillScreen(ST77XX_BLACK);

    display.fillRect(
        0,
        0,
        320,
        38,
        ST77XX_BLUE
    );

    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(2);

    display.setCursor(10, 11);
    display.print("WIFI");

    display.setTextSize(1);
    display.setCursor(245, 15);

    display.print(wifiNetworkCount);
    display.print(" found");

    if (wifiNetworkCount == 0)
    {
        display.setTextSize(2);
        display.setCursor(65, 100);
        display.print("No networks");

        display.setTextSize(1);
        display.setCursor(95, 140);
        display.print("OK = Back");

        return;
    }

    constexpr int visibleRows = 5;

    int firstItem = wifiSelected - 2;

    if (firstItem < 0)
        firstItem = 0;

    if (firstItem > wifiNetworkCount - visibleRows)
        firstItem = wifiNetworkCount - visibleRows;

    if (firstItem < 0)
        firstItem = 0;

    for (int row = 0; row < visibleRows; row++)
    {
        int index = firstItem + row;

        if (index >= wifiNetworkCount)
            break;

        int y = 50 + row * 32;

        if (index == wifiSelected)
        {
            display.fillRoundRect(
                6,
                y - 4,
                308,
                27,
                4,
                ST77XX_WHITE
            );

            display.setTextColor(ST77XX_BLACK);
        }
        else
        {
            display.setTextColor(ST77XX_WHITE);
        }

        display.setTextSize(1);
        display.setCursor(12, y);

        String name = wifiNetworks[index].ssid;

        if (name.length() == 0)
            name = "[Hidden]";

        if (name.length() > 22)
            name = name.substring(0, 22);

        display.print(name);

        if (wifiNetworks[index].savedProfile)
            display.print(" *");

        display.setCursor(235, y);
        display.print(wifiNetworks[index].rssi);
        display.print(" dBm");
    }

    display.setTextColor(ST77XX_WHITE);

    display.setCursor(8, 225);
    display.print("UP");

    display.setCursor(105, 225);
    display.print("OK / HOLD=MENU");

    display.setCursor(280, 225);
    display.print("DOWN");
}

bool connectToWiFi(const String& requestedSsid = "")
{
    stopWiFiGuard();

    Serial.println();
    Serial.println("WiFi connection requested");

    SentinelWiFiProfile savedProfile;
    String selectedSsid;
    String selectedPassword;
    bool usesSavedProfile = false;

    if (requestedSsid.length() > 0 &&
        wifiProfiles.find(requestedSsid, savedProfile))
    {
        selectedSsid = savedProfile.ssid;
        selectedPassword = savedProfile.password;
        usesSavedProfile = true;
    }
    else if (requestedSsid.length() == 0 &&
             wifiProfiles.preferred(savedProfile))
    {
        selectedSsid = savedProfile.ssid;
        selectedPassword = savedProfile.password;
        usesSavedProfile = true;
    }
    else if (WIFI_SSID[0] != '\0' &&
             (requestedSsid.length() == 0 || requestedSsid == WIFI_SSID))
    {
        selectedSsid = WIFI_SSID;
        selectedPassword = WIFI_PASSWORD;
    }

    if (selectedSsid.length() == 0)
    {
        display.fillScreen(ST77XX_BLACK);
        display.fillRect(0, 0, 320, 38, ST77XX_BLUE);
        display.setTextColor(ST77XX_WHITE);
        display.setTextSize(2);
        display.setCursor(10, 11);
        display.print("WIFI SETUP");

        display.setTextSize(1);
        display.setTextColor(ST77XX_RED);
        display.setCursor(10, 70);
        display.print("WiFi is not configured");
        display.setTextColor(ST77XX_WHITE);
        display.setCursor(10, 100);
        display.print("Save profile over USB:");
        display.setCursor(10, 116);
        display.print("WIFI_PROFILE_HELP");

        Serial.println("WiFi profile not found; use WIFI_PROFILE_HELP over USB");
        return false;
    }

    Serial.printf("SSID: %s\n", selectedSsid.c_str());
    Serial.printf("Password length: %u\n", selectedPassword.length());

    display.fillScreen(ST77XX_BLACK);

    display.fillRect(
        0,
        0,
        320,
        38,
        ST77XX_BLUE
    );

    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(2);

    display.setCursor(10, 11);
    display.print("WIFI CONNECT");

    display.setTextSize(1);

    display.setCursor(10, 60);
    display.print("SSID:");

    display.setCursor(70, 60);
    display.print(selectedSsid);

    display.setCursor(10, 90);
    display.print("Connecting...");

    WiFi.mode(WIFI_STA);

    WiFi.begin(selectedSsid.c_str(), selectedPassword.c_str());
    selectedPassword = "";

    unsigned long start = millis();

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - start < 15000
    )
    {
        delay(250);
    }

    display.fillScreen(ST77XX_BLACK);

    display.fillRect(
        0,
        0,
        320,
        38,
        ST77XX_BLUE
    );

    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(2);

    display.setCursor(10, 11);
    display.print("WIFI");

    display.setTextSize(1);

    if (WiFi.status() == WL_CONNECTED)
    {
        if (usesSavedProfile)
            wifiProfiles.markPreferred(selectedSsid);

        display.setTextColor(ST77XX_GREEN);

        display.setCursor(10, 60);
        display.print("CONNECTED");

        display.setTextColor(ST77XX_WHITE);

        display.setCursor(10, 90);
        display.print("IP:");

        display.setCursor(70, 90);
        display.print(
            WiFi.localIP()
        );

        display.setCursor(10, 110);
        display.print("Gateway:");

        display.setCursor(70, 110);
        display.print(
            WiFi.gatewayIP()
        );

        display.setCursor(10, 130);
        display.print("Subnet:");

        display.setCursor(70, 130);
        display.print(
            WiFi.subnetMask()
        );

        Serial.println();
        Serial.println("WiFi connected");

        Serial.print("IP: ");
        Serial.println(
            WiFi.localIP()
        );

        Serial.print("Gateway: ");
        Serial.println(
            WiFi.gatewayIP()
        );

        Serial.print("Subnet: ");
        Serial.println(
            WiFi.subnetMask()
        );

        return true;
    }

    display.setTextColor(ST77XX_RED);

    display.setCursor(10, 80);
    display.print("CONNECTION FAILED");

    Serial.println(
        "WiFi connection failed"
    );

    return false;
}

void resetWiFiCSIStats()
{
    portENTER_CRITICAL(&csiStatsMux);
    csiPacketCount = 0;
    csiLastRssi = 0;
    csiMeanMagnitude = 0;
    csiMagnitudeDelta = 0;
    portEXIT_CRITICAL(&csiStatsMux);

    csiRateReferencePackets = 0;
    csiPacketsPerSecond = 0;
    csiRateReferenceMs = millis();
}

void wifiCSIReceive(void*, wifi_csi_info_t* info)
{
    if (info == nullptr || info->buf == nullptr || info->len < 2)
        return;

    uint16_t start = info->first_word_invalid && info->len > 4
        ? 4
        : 0;

    uint32_t magnitudeSum = 0;
    uint16_t complexSamples = 0;

    for (uint16_t i = start; i + 1 < info->len; i += 2)
    {
        int imaginary = info->buf[i];
        int real = info->buf[i + 1];

        magnitudeSum += abs(imaginary) + abs(real);
        complexSamples++;
    }

    if (complexSamples == 0)
        return;

    uint32_t meanMagnitude = magnitudeSum / complexSamples;

    portENTER_CRITICAL(&csiStatsMux);

    uint32_t previousMagnitude = csiMeanMagnitude;
    uint32_t delta = previousMagnitude > meanMagnitude
        ? previousMagnitude - meanMagnitude
        : meanMagnitude - previousMagnitude;

    csiPacketCount++;
    csiLastRssi = info->rx_ctrl.rssi;
    csiMeanMagnitude = meanMagnitude;
    csiMagnitudeDelta = previousMagnitude == 0
        ? 0
        : (csiMagnitudeDelta * 7 + delta) / 8;

    portEXIT_CRITICAL(&csiStatsMux);
}

void drawWiFiCSI()
{
    uint32_t packets;
    int32_t rssi;
    uint32_t magnitude;
    uint32_t delta;

    portENTER_CRITICAL(&csiStatsMux);
    packets = csiPacketCount;
    rssi = csiLastRssi;
    magnitude = csiMeanMagnitude;
    delta = csiMagnitudeDelta;
    portEXIT_CRITICAL(&csiStatsMux);

    uint32_t now = millis();
    uint32_t elapsed = now - csiRateReferenceMs;

    if (elapsed >= 1000)
    {
        csiPacketsPerSecond =
            (packets - csiRateReferencePackets) * 1000UL / elapsed;

        csiRateReferencePackets = packets;
        csiRateReferenceMs = now;
    }

    display.fillScreen(ST77XX_BLACK);
    display.fillRect(0, 0, 320, 38, ST77XX_BLUE);
    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(2);
    display.setCursor(10, 11);
    display.print("WIFI CSI");

    display.setTextSize(1);

    display.setTextColor(csiActive ? ST77XX_GREEN : ST77XX_RED);
    display.setCursor(245, 15);
    display.print(csiActive ? "LIVE" : "STOPPED");

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 55);
    display.print("SSID:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(95, 55);
    String ssid = WiFi.SSID();
    if (ssid.length() > 27)
        ssid = ssid.substring(0, 27);
    display.print(ssid.length() > 0 ? ssid : "Not connected");

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 80);
    display.print("Channel:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(95, 80);
    display.print(WiFi.channel());

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 105);
    display.print("Frames:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(95, 105);
    display.print(packets);

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 130);
    display.print("Rate:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(95, 130);
    display.print(csiPacketsPerSecond);
    display.print(" fps");

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 155);
    display.print("RSSI:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(95, 155);
    if (packets > 0)
    {
        display.print(rssi);
        display.print(" dBm");
    }
    else
    {
        display.print("Waiting");
    }

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 180);
    display.print("Mean |I|+|Q|:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(135, 180);
    display.print(magnitude);

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 200);
    display.print("Delta EMA:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(95, 200);
    display.print(delta);

    display.drawFastHLine(0, 216, 320, ST77XX_WHITE);
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(8, 225);
    display.print("RESET");
    display.setCursor(140, 225);
    display.print("BACK");
    display.setCursor(280, 225);
    display.print(csiActive ? "STOP" : "START");

    csiLastDrawMs = now;
}

bool startWiFiCSI()
{
    if (csiActive)
        return true;

    stopWiFiGuard();

    if (WiFi.status() != WL_CONNECTED && !connectToWiFi())
    {
        csiLastDrawMs = millis();
        return false;
    }

    resetWiFiCSIStats();

    wifi_csi_config_t config = {};
    config.lltf_en = true;
    config.htltf_en = true;
    config.stbc_htltf2_en = true;
    config.ltf_merge_en = true;
    config.channel_filter_en = false;
    config.manu_scale = false;
    config.shift = 0;

    wifi_promiscuous_filter_t filter = {};
    filter.filter_mask =
        WIFI_PROMIS_FILTER_MASK_MGMT |
        WIFI_PROMIS_FILTER_MASK_DATA;

    esp_err_t result = esp_wifi_set_ps(WIFI_PS_NONE);

    if (result == ESP_OK)
        result = esp_wifi_set_promiscuous_filter(&filter);

    if (result == ESP_OK)
        result = esp_wifi_set_csi_config(&config);

    if (result == ESP_OK)
        result = esp_wifi_set_csi_rx_cb(wifiCSIReceive, nullptr);

    if (result == ESP_OK)
        result = esp_wifi_set_promiscuous(true);

    if (result == ESP_OK)
        result = esp_wifi_set_csi(true);

    if (result != ESP_OK)
    {
        esp_wifi_set_csi(false);
        esp_wifi_set_promiscuous(false);
        esp_wifi_set_csi_rx_cb(nullptr, nullptr);
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

        Serial.printf(
            "CSI start failed: %s (%d)\n",
            esp_err_to_name(result),
            result
        );

        csiActive = false;
        drawWiFiCSI();
        return false;
    }

    csiActive = true;
    Serial.println("CSI monitor started (diagnostic metrics only)");
    drawWiFiCSI();
    return true;
}

void stopWiFiCSI()
{
    if (!csiActive)
        return;

    esp_wifi_set_csi(false);
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_csi_rx_cb(nullptr, nullptr);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

    csiActive = false;
    Serial.println("CSI monitor stopped");
}

void handleWiFiCSI()
{
    if (pressed(BTN_TOP, lastTop))
    {
        resetWiFiCSIStats();
        drawWiFiCSI();
    }

    if (pressed(BTN_MIDDLE, lastMiddle))
    {
        stopWiFiCSI();
        currentScreen = Screen::MainMenu;
        drawMenu();
        return;
    }

    if (pressed(BTN_BOTTOM, lastBottom))
    {
        if (csiActive)
            stopWiFiCSI();
        else
            startWiFiCSI();

        drawWiFiCSI();
    }

    if (millis() - csiLastDrawMs >= 500)
        drawWiFiCSI();
}

void scanWiFi()
{
    stopWiFiGuard();
    drawWiFiScanning();

    Serial.println();
    Serial.println("WiFi scan started");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    delay(100);

    int count = WiFi.scanNetworks(
        false,
        true
    );

    wifiNetworkCount = 0;
    wifiSelected = 0;

    if (count < 0)
        count = 0;

    for (int i = 0;
         i < count &&
         wifiNetworkCount < MAX_WIFI_NETWORKS;
         i++)
    {
        WiFiNetworkInfo& saved =
            wifiNetworks[wifiNetworkCount];

        saved.ssid = WiFi.SSID(i);
        saved.bssid = WiFi.BSSIDstr(i);
        saved.rssi = WiFi.RSSI(i);
        saved.channel = WiFi.channel(i);
        saved.savedProfile =
            wifiProfiles.contains(saved.ssid) ||
            (WIFI_SSID[0] != '\0' && saved.ssid == WIFI_SSID);

        saved.security =
            getWiFiSecurity(
                WiFi.encryptionType(i)
            );

        Serial.println("-------------------------");

        Serial.printf(
            "#%d\n",
            wifiNetworkCount
        );

        Serial.printf(
            "SSID: %s\n",
            saved.ssid.length() > 0
                ? saved.ssid.c_str()
                : "[Hidden]"
        );

        Serial.printf(
            "BSSID: %s\n",
            saved.bssid.c_str()
        );

        Serial.printf(
            "RSSI: %d dBm\n",
            saved.rssi
        );

        Serial.printf(
            "Channel: %d\n",
            saved.channel
        );

        Serial.printf(
            "Security: %s\n",
            saved.security.c_str()
        );

        wifiNetworkCount++;
    }

    WiFi.scanDelete();

    // Сортировка: сильнейший сигнал сверху

    for (int i = 0; i < wifiNetworkCount - 1; i++)
    {
        for (int j = 0;
             j < wifiNetworkCount - i - 1;
             j++)
        {
            if (
                wifiNetworks[j].rssi <
                wifiNetworks[j + 1].rssi
            )
            {
                WiFiNetworkInfo temp =
                    wifiNetworks[j];

                wifiNetworks[j] =
                    wifiNetworks[j + 1];

                wifiNetworks[j + 1] =
                    temp;
            }
        }
    }

    Serial.printf(
        "WiFi scan completed: %d networks\n",
        wifiNetworkCount
    );

    drawWiFiResults();
}

void drawWiFiDetails()
{
    if (wifiNetworkCount == 0)
        return;

    WiFiNetworkInfo& network =
        wifiNetworks[wifiSelected];

    display.fillScreen(ST77XX_BLACK);

    display.fillRect(
        0,
        0,
        320,
        34,
        ST77XX_BLUE
    );

    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(2);

    display.setCursor(10, 9);
    display.print("WIFI NETWORK");

    display.setTextSize(1);

    int y = 50;

    // SSID

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, y);
    display.print("SSID:");

    display.setTextColor(ST77XX_WHITE);
    display.setCursor(95, y);

    if (network.ssid.length() > 0)
        display.print(network.ssid);
    else
        display.print("[Hidden]");

    y += 22;

    // BSSID

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, y);
    display.print("BSSID:");

    display.setTextColor(ST77XX_WHITE);
    display.setCursor(95, y);
    display.print(network.bssid);

    y += 22;

    // RSSI

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, y);
    display.print("RSSI:");

    display.setTextColor(ST77XX_WHITE);
    display.setCursor(95, y);
    display.print(network.rssi);
    display.print(" dBm");

    y += 22;

    // Signal

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, y);
    display.print("Signal:");

    display.setTextColor(ST77XX_WHITE);
    display.setCursor(95, y);
    display.print(
        getSignalQuality(network.rssi)
    );

    y += 22;

    // Channel

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, y);
    display.print("Channel:");

    display.setTextColor(ST77XX_WHITE);
    display.setCursor(95, y);
    display.print(network.channel);

    y += 22;

    // Frequency

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, y);
    display.print("Frequency:");

    display.setTextColor(ST77XX_WHITE);
    display.setCursor(95, y);

    int frequency =
        2407 + network.channel * 5;

    display.print(frequency);
    display.print(" MHz");

    y += 22;

    // Security

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, y);
    display.print("Security:");

    display.setTextColor(ST77XX_WHITE);
    display.setCursor(95, y);
    display.print(network.security);

    y += 22;

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, y);
    display.print("Saved profile:");
    display.setTextColor(
        network.savedProfile
            ? ST77XX_GREEN
            : ST77XX_RED
    );
    display.setCursor(105, y);
    display.print(network.savedProfile ? "Yes" : "No");

    // Footer

    display.drawFastHLine(
        0,
        216,
        320,
        ST77XX_WHITE
    );

    display.setCursor(8, 225);
    display.print("RESCAN");

    display.setCursor(115, 225);
    display.print("BACK/HOLD=MENU");

    display.setCursor(260, 225);
    display.print("CONNECT");
}

void handleWiFi()
{
    if (pressed(BTN_TOP, lastTop))
    {
        if (wifiNetworkCount > 0)
        {
            wifiSelected--;

            if (wifiSelected < 0)
                wifiSelected = wifiNetworkCount - 1;

            drawWiFiResults();
        }
    }

    if (pressed(BTN_BOTTOM, lastBottom))
    {
        if (wifiNetworkCount > 0)
        {
            wifiSelected++;

            if (wifiSelected >= wifiNetworkCount)
                wifiSelected = 0;

            drawWiFiResults();
        }
    }

    if (pressed(BTN_MIDDLE, lastMiddle))
    {
        if (wifiNetworkCount > 0)
        {
            currentScreen =
                Screen::WiFiDetails;

            drawWiFiDetails();
        }
        else
        {
            currentScreen =
                Screen::MainMenu;

            drawMenu();
        }
    }
}

void handleWiFiDetails()
{
    if (pressed(BTN_TOP, lastTop))
    {
        currentScreen = Screen::WiFi;

        scanWiFi();
        return;
    }

    if (pressed(BTN_MIDDLE, lastMiddle))
    {
        currentScreen = Screen::WiFi;

        drawWiFiResults();
        return;
    }

    if (pressed(BTN_BOTTOM, lastBottom))
    {
        WiFiNetworkInfo& network = wifiNetworks[wifiSelected];

        if (network.savedProfile)
        {
            connectToWiFi(network.ssid);
            delay(1500);
        }
        else
        {
            display.fillRect(0, 190, 320, 25, ST77XX_BLACK);
            display.setTextColor(ST77XX_RED);
            display.setCursor(10, 198);
            display.print("No saved profile; configure over USB");
            delay(1500);
        }

        currentScreen = Screen::WiFi;
        drawWiFiResults();
        return;
    }
}

void initializeWiFiServices()
{
    wifiProfileConsole.begin();
}

void pollWiFiServices()
{
    wifiProfileConsole.poll();
}

void reportWiFiConfiguration()
{
    const size_t savedProfileCount = wifiProfiles.count();

    if (savedProfileCount == 0 && WIFI_SSID[0] == '\0')
    {
        Serial.println(
            "WiFi profiles are not configured; use WIFI_PROFILE_HELP over USB"
        );
        return;
    }

    Serial.printf(
        "Saved WiFi profiles: %u\n",
        static_cast<unsigned>(savedProfileCount)
    );

    if (WIFI_SSID[0] != '\0')
        Serial.printf("Build-time WiFi SSID: %s\n", WIFI_SSID);
}

void enterWiFiGuard()
{
    stopWiFiCSI();
    wifiGuardScreen.enter();
}

void handleWiFiGuardScreen()
{
    if (!wifiGuardScreen.update(
            pressed(BTN_TOP, lastTop),
            pressed(BTN_MIDDLE, lastMiddle),
            pressed(BTN_BOTTOM, lastBottom)
        ))
    {
        return;
    }

    currentScreen = Screen::MainMenu;
    drawMenu();
}
