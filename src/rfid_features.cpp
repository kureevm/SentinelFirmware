#include "FirmwareApp.h"

#include <MFRC522.h>
#include <SPI.h>

namespace
{
MFRC522 reader(RFID_SS, RFID_RST);
bool readerDetected = false;
uint8_t readerVersion = 0;
String lastUid;
String lastType;
uint32_t lastReadMs = 0;

String uidToString(const MFRC522::Uid& uid)
{
    String value;
    value.reserve(uid.size * 3);

    for (byte i = 0; i < uid.size; i++)
    {
        if (i > 0)
            value += ':';

        if (uid.uidByte[i] < 0x10)
            value += '0';

        value += String(uid.uidByte[i], HEX);
    }

    value.toUpperCase();
    return value;
}

void drawReaderScreen()
{
    display.fillScreen(ST77XX_BLACK);
    display.fillRect(0, 0, 320, 38, ST77XX_BLUE);
    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(2);
    display.setCursor(10, 11);
    display.print("RFID READER");
    display.setTextSize(1);

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 55);
    display.print("RC522:");
    display.setCursor(105, 55);
    display.setTextColor(readerDetected ? ST77XX_GREEN : ST77XX_RED);
    display.print(readerDetected ? "READY" : "NOT FOUND");

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 77);
    display.print("Version:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(105, 77);

    if (readerDetected)
    {
        display.print("0x");

        if (readerVersion < 0x10)
            display.print('0');

        display.print(readerVersion, HEX);
    }
    else
    {
        display.print("N/A");
    }

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 110);
    display.print("UID:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(60, 110);
    display.print(lastUid.length() > 0 ? lastUid : "Present a tag");

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 135);
    display.print("Type:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(60, 135);
    display.print(lastType.length() > 0 ? lastType : "N/A");

    display.setTextColor(ST77XX_WHITE);
    display.setCursor(10, 175);

    if (!readerDetected)
        display.print("Check 3.3V, SPI, SS=7, RST=14");
    else
        display.print("Read-only UID mode");

    display.drawFastHLine(0, 216, 320, ST77XX_WHITE);
    display.setCursor(8, 225);
    display.print("REINIT");
    display.setCursor(145, 225);
    display.print("BACK");
    display.setCursor(280, 225);
    display.print("CLEAR");
}

void initializeReader()
{
    pinMode(TFT_CS, OUTPUT);
    digitalWrite(TFT_CS, HIGH);
    pinMode(RFID_SS, OUTPUT);
    digitalWrite(RFID_SS, HIGH);

    SPI.begin(SCK, MISO, MOSI);
    reader.PCD_Init();
    delay(50);

    readerVersion = reader.PCD_ReadRegister(MFRC522::VersionReg);
    readerDetected = readerVersion != 0x00 && readerVersion != 0xFF;

    Serial.printf(
        "RC522 version: 0x%02X (%s)\n",
        readerVersion,
        readerDetected ? "detected" : "not detected"
    );
}

void pollReader()
{
    if (!readerDetected || millis() - lastReadMs < 250)
        return;

    if (!reader.PICC_IsNewCardPresent() || !reader.PICC_ReadCardSerial())
        return;

    lastUid = uidToString(reader.uid);
    const MFRC522::PICC_Type type = reader.PICC_GetType(reader.uid.sak);
    lastType = reader.PICC_GetTypeName(type);
    lastReadMs = millis();

    Serial.print("RFID UID: ");
    Serial.println(lastUid);
    Serial.print("RFID type: ");
    Serial.println(lastType);

    reader.PICC_HaltA();
    reader.PCD_StopCrypto1();
    drawReaderScreen();
}
}

void enterRFID()
{
    initializeReader();
    drawReaderScreen();
}

void handleRFID()
{
    if (pressed(BTN_TOP, lastTop))
    {
        initializeReader();
        drawReaderScreen();
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
        lastUid = "";
        lastType = "";
        drawReaderScreen();
        return;
    }

    pollReader();
}
