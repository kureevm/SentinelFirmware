#include "FirmwareApp.h"

#include <NimBLEDevice.h>

namespace
{
constexpr int MAX_BLE_DEVICES = 20;

struct BLEDeviceInfo
{
    String name;
    String address;
    String addressType;
    int rssi;
    bool hasTxPower;
    int txPower;
    bool hasManufacturer;
    uint16_t manufacturerId;
    String manufacturerName;
    String manufacturerData;
    bool hasServiceUUID;
    String serviceUUID;
};

BLEDeviceInfo bleDevices[MAX_BLE_DEVICES];
int bleDeviceCount = 0;
int bleSelected = 0;
}

// ================= BLE SCANNING SCREEN =================

void drawBLEScanning()
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
    display.print("BLE SCANNER");

    display.setTextColor(ST77XX_GREEN);
    display.setTextSize(2);

    display.setCursor(75, 95);
    display.print("Scanning...");

    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(1);

    display.setCursor(85, 140);
    display.print("Please wait 5 sec");
}

// ================= BLE RESULTS =================

void drawBLEResults()
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
    display.print("BLE");

    display.setTextSize(1);
    display.setCursor(245, 15);

    display.print(bleDeviceCount);
    display.print(" found");

    if (bleDeviceCount == 0)
    {
        display.setTextSize(2);
        display.setCursor(65, 100);
        display.print("No devices");

        display.setTextSize(1);
        display.setCursor(95, 140);
        display.print("OK = Back");

        return;
    }

    constexpr int visibleRows = 5;

    int firstItem = bleSelected - 2;

    if (firstItem < 0)
        firstItem = 0;

    if (firstItem > bleDeviceCount - visibleRows)
        firstItem = bleDeviceCount - visibleRows;

    if (firstItem < 0)
        firstItem = 0;

    for (int row = 0; row < visibleRows; row++)
    {
        int index = firstItem + row;

        if (index >= bleDeviceCount)
            break;

        int y = 50 + row * 32;

        if (index == bleSelected)
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

    if (bleDevices[index].name.length() > 0)
    {
        String shortName = bleDevices[index].name;

        if (shortName.length() > 22)
        {
            shortName = shortName.substring(0, 22);
        }

        display.print(shortName);
    }
    else if (
        bleDevices[index].hasManufacturer &&
        bleDevices[index].manufacturerName != "Unknown"
    )
    {
        display.print("[");

        display.print(
            bleDevices[index].manufacturerName
        );

        display.print("]");
    }
    else
    {
        display.print("[Unknown]");
    }

        display.setCursor(235, y);

        display.print(
            bleDevices[index].rssi
        );

        display.print(" dBm");
    }

    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(1);

    display.setCursor(8, 225);
    display.print("UP");

    display.setCursor(135, 225);
    display.print("OK BACK");

    display.setCursor(280, 225);
    display.print("DOWN");
}

void drawBLEDetails()
{
    if (bleDeviceCount == 0)
        return;

    BLEDeviceInfo& device =
        bleDevices[bleSelected];

    display.fillScreen(ST77XX_BLACK);

    // Header
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
    display.print("BLE DEVICE");

    display.setTextSize(1);

    int y = 45;

    // ---------- NAME ----------

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, y);
    display.print("Name:");

    display.setTextColor(ST77XX_WHITE);
    display.setCursor(95, y);

    if (device.name.length() > 0)
    {
        String name = device.name;

        if (name.length() > 30)
            name = name.substring(0, 30);

        display.print(name);
    }
    else
    {
        display.print("[Unknown]");
    }

    y += 20;

    // ---------- ADDRESS ----------

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, y);
    display.print("Address:");

    display.setTextColor(ST77XX_WHITE);
    display.setCursor(95, y);
    display.print(device.address);

    y += 20;

    // ---------- ADDRESS TYPE ----------

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, y);
    display.print("Addr type:");

    display.setTextColor(ST77XX_WHITE);
    display.setCursor(95, y);
    display.print(device.addressType);

    y += 20;

 // ---------- RSSI ----------

display.setTextColor(ST77XX_GREEN);
display.setCursor(10, y);
display.print("RSSI:");

display.setTextColor(ST77XX_WHITE);
display.setCursor(95, y);
display.print(device.rssi);
display.print(" dBm");

y += 20;

// ---------- SIGNAL ----------

display.setTextColor(ST77XX_GREEN);
display.setCursor(10, y);
display.print("Signal:");

display.setTextColor(ST77XX_WHITE);
display.setCursor(95, y);
display.print(getSignalQuality(device.rssi));

y += 20;

// ---------- TX POWER ----------

display.setTextColor(ST77XX_GREEN);
display.setCursor(10, y);
display.print("TX Power:");

display.setTextColor(ST77XX_WHITE);
display.setCursor(95, y);

if (device.hasTxPower)
{
    display.print(device.txPower);
    display.print(" dBm");
}
else
{
    display.print("N/A");
}

y += 20;
    // ---------- MANUFACTURER ----------

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, y);
    display.print("Vendor:");

    display.setTextColor(ST77XX_WHITE);
    display.setCursor(95, y);

    if (device.hasManufacturer)
    {
        display.print(device.manufacturerName);

        display.print(" 0x");

        if (device.manufacturerId < 0x1000)
            display.print("0");

        if (device.manufacturerId < 0x0100)
            display.print("0");

        if (device.manufacturerId < 0x0010)
            display.print("0");

        display.print(
            device.manufacturerId,
            HEX
        );
    }
    else
    {
        display.print("N/A");
    }

    y += 20;

    // ---------- SERVICE UUID ----------

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, y);
    display.print("Service:");

    display.setTextColor(ST77XX_WHITE);
    display.setCursor(95, y);

    if (device.hasServiceUUID)
    {
        String uuid =
            device.serviceUUID;

        if (uuid.length() > 32)
            uuid = uuid.substring(0, 32);

        display.print(uuid);
    }
    else
    {
        display.print("N/A");
    }

    // ---------- FOOTER ----------

    display.drawFastHLine(
        0,
        216,
        320,
        ST77XX_WHITE
    );

    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(1);

    display.setCursor(8, 225);
    display.print("RESCAN");

    display.setCursor(135, 225);
    display.print("BACK");

    display.setCursor(270, 225);
    display.print("MENU");
}
// ================= BLE SCAN =================

String getManufacturerName(uint16_t id)
{
    switch (id)
    {
        case 0x004C:
            return "Apple";

        case 0x0006:
            return "Microsoft";

        case 0x0075:
            return "Samsung";

        case 0x038F:
            return "Xiaomi";

        case 0x00E0:
            return "Google";

        default:
            return "Unknown";
    }
}

String bytesToHex(const std::string& data)
{
    String result;

    for (size_t i = 0; i < data.length(); i++)
    {
        char buffer[4];

        snprintf(
            buffer,
            sizeof(buffer),
            "%02X",
            (uint8_t)data[i]
        );

        result += buffer;

        if (i + 1 < data.length())
            result += " ";
    }

    return result;
}
void sortBLEDevicesByRSSI()
{
    for (int i = 0; i < bleDeviceCount - 1; i++)
    {
        for (int j = 0; j < bleDeviceCount - i - 1; j++)
        {
            // -40 сильнее, чем -80,
            // поэтому большее значение RSSI должно быть выше.
            if (bleDevices[j].rssi < bleDevices[j + 1].rssi)
            {
                BLEDeviceInfo temp = bleDevices[j];

                bleDevices[j] = bleDevices[j + 1];
                bleDevices[j + 1] = temp;
            }
        }
    }
}
void scanBLE()
{
    stopWiFiGuard();
    drawBLEScanning();

    Serial.println();
    Serial.println("BLE scan started");

    NimBLEScan* scanner =
        NimBLEDevice::getScan();

    scanner->setActiveScan(true);

    scanner->setInterval(100);
    scanner->setWindow(100);

    scanner->clearResults();

    NimBLEScanResults results =
        scanner->getResults(
            5000,
            false
        );

    bleDeviceCount = 0;
    bleSelected = 0;

    int count = results.getCount();

    Serial.printf(
        "BLE devices found: %d\n",
        count
    );

    for (int i = 0;
         i < count &&
         bleDeviceCount < MAX_BLE_DEVICES;
         i++)
    {
        const NimBLEAdvertisedDevice* device =
            results.getDevice(i);

        if (device == nullptr)
            continue;

    BLEDeviceInfo& saved =
        bleDevices[bleDeviceCount];

    // ---------- BASIC ----------

    saved.name =
        device->getName().c_str();

    saved.address =
        device->getAddress()
            .toString()
            .c_str();

    saved.rssi =
        device->getRSSI();

    // ---------- ADDRESS TYPE ----------

    uint8_t addressType =
        device->getAddressType();

    if (addressType == 0)
    {
        saved.addressType = "Public";
    }
    else if (addressType == 1)
    {
        saved.addressType = "Random";
    }
    else
    {
        saved.addressType = "Unknown";
    }

    // ---------- TX POWER ----------

    saved.hasTxPower =
        device->haveTXPower();

    if (saved.hasTxPower)
    {
        saved.txPower =
            device->getTXPower();
    }
    else
    {
        saved.txPower = 0;
    }

    // ---------- MANUFACTURER ----------

    saved.hasManufacturer =
        device->haveManufacturerData();

    saved.manufacturerId = 0;
    saved.manufacturerName = "";
    saved.manufacturerData = "";

    if (saved.hasManufacturer)
    {
        std::string data =
            device->getManufacturerData();

        saved.manufacturerData =
            bytesToHex(data);

        if (data.length() >= 2)
        {
            saved.manufacturerId =
                (uint8_t)data[0] |
                ((uint16_t)(uint8_t)data[1] << 8);

            saved.manufacturerName =
                getManufacturerName(
                    saved.manufacturerId
                );
        }
    }

    // ---------- SERVICE UUID ----------

    saved.hasServiceUUID =
        device->haveServiceUUID();

    saved.serviceUUID = "";

    if (saved.hasServiceUUID)
    {
        saved.serviceUUID =
            device->getServiceUUID()
                .toString()
                .c_str();
    }

    Serial.println("--------------------------------");

    Serial.printf(
        "#%d\n",
        bleDeviceCount
    );

    Serial.printf(
        "Name: %s\n",
        saved.name.length() > 0
            ? saved.name.c_str()
            : "[Unknown]"
    );

    Serial.printf(
        "Address: %s\n",
        saved.address.c_str()
    );

    Serial.printf(
        "Address type: %s\n",
        saved.addressType.c_str()
    );

    Serial.printf(
        "RSSI: %d dBm\n",
        saved.rssi
    );

    if (saved.hasTxPower)
    {
        Serial.printf(
            "TX Power: %d dBm\n",
            saved.txPower
        );
    }

    if (saved.hasManufacturer)
    {
        Serial.printf(
            "Vendor: %s (0x%04X)\n",
            saved.manufacturerName.c_str(),
            saved.manufacturerId
        );

        Serial.printf(
            "Manufacturer Data: %s\n",
            saved.manufacturerData.c_str()
        );
    }

    if (saved.hasServiceUUID)
    {
        Serial.printf(
            "Service UUID: %s\n",
            saved.serviceUUID.c_str()
        );
    }

        bleDeviceCount++;
    }

    scanner->clearResults();

    sortBLEDevicesByRSSI();

    bleSelected = 0;

    Serial.println("BLE scan completed");
    Serial.println("Sorted by RSSI");

    drawBLEResults();
}

// ================= BLE INPUT =================

void handleBluetooth()
{
    if (pressed(
        BTN_TOP,
        lastTop
    ))
    {
        if (bleDeviceCount > 0)
        {
            bleSelected--;

            if (bleSelected < 0)
            {
                bleSelected =
                    bleDeviceCount - 1;
            }

            drawBLEResults();
        }
    }

    if (pressed(
        BTN_BOTTOM,
        lastBottom
    ))
    {
        if (bleDeviceCount > 0)
        {
            bleSelected++;

            if (
                bleSelected >=
                bleDeviceCount
            )
            {
                bleSelected = 0;
            }

            drawBLEResults();
        }
    }

    if (pressed( BTN_MIDDLE, lastMiddle))
    {
        if (bleDeviceCount > 0)
        {
            currentScreen =
                Screen::BLEDetails;

            drawBLEDetails();
        }
        else
        {
            currentScreen = Screen::MainMenu;
            drawMenu();
        }
    }
}
void handleBLEDetails()
{
    // Верхняя кнопка = новое сканирование
    if (pressed(
        BTN_TOP,
        lastTop
    ))
    {
        currentScreen =
            Screen::Bluetooth;

        scanBLE();

        return;
    }

    // Средняя = назад к списку
    if (pressed(
        BTN_MIDDLE,
        lastMiddle
    ))
    {
        currentScreen =
            Screen::Bluetooth;

        drawBLEResults();

        return;
    }

    // Нижняя = сразу назад в главное меню
    if (pressed(
        BTN_BOTTOM,
        lastBottom
    ))
    {
        currentScreen =
            Screen::MainMenu;

        drawMenu();

        return;
    }
}
