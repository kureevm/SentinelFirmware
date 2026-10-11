#include "FirmwareApp.h"
#include "StorageFeatures.h"

#include <MFRC522.h>
#include <SPI.h>
#include <esp_system.h>

namespace
{
constexpr byte SENTINEL_TOKEN_BLOCK = 4;
constexpr uint32_t WRITE_ARM_HOLD_MS = 3000;
constexpr uint32_t WRITE_ARM_WINDOW_MS = 10000;

MFRC522 reader(RFID_SS, RFID_RST);
bool readerDetected = false;
uint8_t readerVersion = 0;
String lastUid;
String lastType;
String lastToken = "none";
String writeStatus = "safe read mode";
uint32_t lastReadMs = 0;
uint32_t readCount = 0;
String lastLogStatus = "SD unavailable";
bool writeArmed = false;
bool writeArmTriggered = false;
uint32_t writeArmHoldStartedAt = 0;
uint32_t writeArmExpiresAt = 0;

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

bool isClassicType(MFRC522::PICC_Type type)
{
    return type == MFRC522::PICC_TYPE_MIFARE_MINI ||
           type == MFRC522::PICC_TYPE_MIFARE_1K ||
           type == MFRC522::PICC_TYPE_MIFARE_4K;
}

MFRC522::StatusCode authenticateTokenBlock()
{
    MFRC522::MIFARE_Key key;
    memset(key.keyByte, 0xFF, sizeof(key.keyByte));
    return reader.PCD_Authenticate(
        MFRC522::PICC_CMD_MF_AUTH_KEY_A,
        SENTINEL_TOKEN_BLOCK,
        &key,
        &reader.uid
    );
}

bool hasSentinelPrefix(const byte* data)
{
    return data[0] == 'S' && data[1] == 'N' &&
           data[2] == 'T' && data[3] == 'L' && data[4] == '-';
}

bool isEmptyTokenBlock(const byte* data)
{
    bool allZero = true;
    bool allFF = true;

    for (byte index = 0; index < 16; index++)
    {
        allZero = allZero && data[index] == 0x00;
        allFF = allFF && data[index] == 0xFF;
    }

    return allZero || allFF;
}

String tokenToString(const byte* data)
{
    char text[17];
    memcpy(text, data, 16);
    text[16] = '\0';
    return String(text);
}

String createToken(byte* data)
{
    char text[17];
    snprintf(
        text,
        sizeof(text),
        "SNTL-%08lX-V1",
        static_cast<unsigned long>(esp_random())
    );
    memcpy(data, text, 16);
    return String(text);
}

void readSentinelToken(MFRC522::PICC_Type type)
{
    if (!isClassicType(type))
    {
        lastToken = "unsupported type";
        return;
    }

    MFRC522::StatusCode status = authenticateTokenBlock();
    if (status != MFRC522::STATUS_OK)
    {
        lastToken = "locked/non-Sentinel";
        reader.PCD_StopCrypto1();
        return;
    }

    byte buffer[18];
    byte size = sizeof(buffer);
    status = reader.MIFARE_Read(SENTINEL_TOKEN_BLOCK, buffer, &size);

    if (status == MFRC522::STATUS_OK && hasSentinelPrefix(buffer))
        lastToken = tokenToString(buffer);
    else
        lastToken = "not Sentinel";

    reader.PCD_StopCrypto1();
}

bool writeSentinelToken(MFRC522::PICC_Type type)
{
    writeArmed = false;

    if (!isClassicType(type))
    {
        writeStatus = "blocked: Classic only";
        lastToken = "unsupported type";
        return false;
    }

    MFRC522::StatusCode status = authenticateTokenBlock();
    if (status != MFRC522::STATUS_OK)
    {
        writeStatus = "blocked: default key failed";
        lastToken = "locked/non-Sentinel";
        reader.PCD_StopCrypto1();
        return false;
    }

    byte current[18];
    byte currentSize = sizeof(current);
    status = reader.MIFARE_Read(SENTINEL_TOKEN_BLOCK, current, &currentSize);

    if (status != MFRC522::STATUS_OK)
    {
        writeStatus = "blocked: cannot read block 4";
        reader.PCD_StopCrypto1();
        return false;
    }

    if (!isEmptyTokenBlock(current) && !hasSentinelPrefix(current))
    {
        writeStatus = "blocked: block 4 contains data";
        lastToken = "foreign data preserved";
        reader.PCD_StopCrypto1();
        Serial.println("RFID write blocked: block 4 is not empty Sentinel data");
        return false;
    }

    byte tokenData[16];
    const String generatedToken = createToken(tokenData);
    status = reader.MIFARE_Write(
        SENTINEL_TOKEN_BLOCK,
        tokenData,
        sizeof(tokenData)
    );

    if (status != MFRC522::STATUS_OK)
    {
        writeStatus = "write failed";
        reader.PCD_StopCrypto1();
        return false;
    }

    byte verify[18];
    byte verifySize = sizeof(verify);
    status = reader.MIFARE_Read(SENTINEL_TOKEN_BLOCK, verify, &verifySize);
    reader.PCD_StopCrypto1();

    if (status != MFRC522::STATUS_OK || memcmp(tokenData, verify, 16) != 0)
    {
        writeStatus = "verify failed";
        return false;
    }

    lastToken = generatedToken;
    writeStatus = "written + verified";
    Serial.println("Sentinel token written and verified in MIFARE block 4");
    return true;
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
    display.setCursor(10, 52);
    display.print("RC522:");
    display.setCursor(65, 52);
    display.setTextColor(readerDetected ? ST77XX_GREEN : ST77XX_RED);
    display.print(readerDetected ? "READY" : "NOT FOUND");

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(180, 52);
    display.print("Ver:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(215, 52);

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
    display.setCursor(10, 76);
    display.print("UID:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(50, 76);
    display.print(lastUid.length() > 0 ? lastUid : "Present a tag");

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 99);
    display.print("Type:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(50, 99);
    display.print(lastType.length() > 0 ? lastType : "N/A");

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 122);
    display.print("Token:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(60, 122);
    display.print(lastToken);

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 145);
    display.print("Reads:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(60, 145);
    display.print(readCount);

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(120, 145);
    display.print("SD:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(145, 145);
    display.print(lastLogStatus);

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 168);
    display.print("Write:");
    display.setTextColor(writeArmed ? ST77XX_YELLOW : ST77XX_WHITE);
    display.setCursor(60, 168);

    if (!readerDetected)
        display.print("Check 3.3V, SPI, SS=7, RST=14");
    else
        display.print(writeArmed ? "ARMED - present OWN tag" : writeStatus);

    display.setTextColor(ST77XX_YELLOW);
    display.setCursor(10, 193);
    display.print("Hold REINIT+CLEAR 3s: arm write");

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

    if (writeArmed)
        writeSentinelToken(type);
    else
        readSentinelToken(type);

    reader.PICC_HaltA();
    reader.PCD_StopCrypto1();
    digitalWrite(RFID_SS, HIGH);

    readCount++;
    if (isSDCardMounted())
        lastLogStatus = logRFIDRead(lastUid, lastType) ? "saved" : "write error";
    else
        lastLogStatus = "not mounted";

    drawReaderScreen();
}
}

void enterRFID()
{
    writeArmed = false;
    writeStatus = "safe read mode";
    writeArmHoldStartedAt = 0;
    writeArmTriggered = false;
    initializeReader();
    drawReaderScreen();
}

void handleRFID()
{
    if (writeArmed &&
        static_cast<int32_t>(millis() - writeArmExpiresAt) >= 0)
    {
        writeArmed = false;
        writeStatus = "arm expired; no write";
        drawReaderScreen();
    }

    const bool topHeld = digitalRead(BTN_TOP) == LOW;
    const bool bottomHeld = digitalRead(BTN_BOTTOM) == LOW;

    if (readerDetected && topHeld && bottomHeld)
    {
        lastTop = LOW;
        lastBottom = LOW;

        if (writeArmHoldStartedAt == 0)
        {
            writeArmHoldStartedAt = millis();
            writeStatus = "hold both buttons...";
            drawReaderScreen();
        }

        if (!writeArmTriggered &&
            millis() - writeArmHoldStartedAt >= WRITE_ARM_HOLD_MS)
        {
            writeArmTriggered = true;
            writeArmed = true;
            writeArmExpiresAt = millis() + WRITE_ARM_WINDOW_MS;
            writeStatus = "armed for 10 seconds";
            drawReaderScreen();
        }

        return;
    }

    if (!topHeld || !bottomHeld)
    {
        writeArmHoldStartedAt = 0;
        writeArmTriggered = false;
    }

    if (pressed(BTN_TOP, lastTop))
    {
        writeArmed = false;
        writeStatus = "safe read mode";
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
        lastToken = "none";
        writeStatus = "safe read mode";
        drawReaderScreen();
        return;
    }

    pollReader();
}
