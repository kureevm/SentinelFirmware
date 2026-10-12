#include "FirmwareApp.h"
#include "StorageFeatures.h"

#include <MFRC522.h>
#include <Preferences.h>
#include <SPI.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>

namespace
{
constexpr byte SENTINEL_TOKEN_BLOCK = 4;
constexpr uint32_t WRITE_ARM_WINDOW_MS = 10000;
constexpr uint32_t VERIFY_ARM_WINDOW_MS = 10000;
constexpr uint8_t RFID_ACTION_COUNT = 7;
constexpr char RFID_PREFERENCES_NAMESPACE[] = "sent-rfid";

enum class RfidAction : uint8_t
{
    ReadTag,
    SaveIdentity,
    VerifySaved,
    WriteToken,
    DeleteSaved,
    Reinitialize,
    Exit
};

const char* const RFID_ACTION_LABELS[RFID_ACTION_COUNT] = {
    "READ TAG",
    "SAVE ID",
    "VERIFY SAVED",
    "WRITE TOKEN",
    "DELETE SAVED",
    "REINIT READER",
    "EXIT"
};

MFRC522 reader(RFID_SS, RFID_RST);
bool readerDetected = false;
uint8_t readerVersion = 0;
String lastUid;
String lastType;
String lastToken = "none";
String actionStatus = "safe read mode";
String savedFingerprint;
uint32_t lastReadMs = 0;
uint32_t readCount = 0;
String lastLogStatus = "SD unavailable";
bool writeArmed = false;
uint32_t writeArmExpiresAt = 0;
bool verifyArmed = false;
uint32_t verifyArmExpiresAt = 0;
uint8_t selectedAction = 0;

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

String fingerprintUid(const String& uid)
{
    unsigned char digest[32];
    const int result = mbedtls_sha256_ret(
        reinterpret_cast<const unsigned char*>(uid.c_str()),
        uid.length(),
        digest,
        0
    );

    if (result != 0)
        return "";

    static const char HEX_DIGITS[] = "0123456789ABCDEF";
    char encoded[65];

    for (size_t index = 0; index < sizeof(digest); index++)
    {
        encoded[index * 2] = HEX_DIGITS[digest[index] >> 4];
        encoded[index * 2 + 1] = HEX_DIGITS[digest[index] & 0x0F];
    }

    encoded[64] = '\0';
    return String(encoded);
}

void loadSavedIdentity()
{
    Preferences preferences;

    if (!preferences.begin(RFID_PREFERENCES_NAMESPACE, true))
    {
        savedFingerprint = "";
        return;
    }

    savedFingerprint = preferences.getString("fp", "");
    preferences.end();
}

bool saveCurrentIdentity()
{
    if (lastUid.length() == 0)
    {
        actionStatus = "read a tag first";
        return false;
    }

    const String fingerprint = fingerprintUid(lastUid);
    if (fingerprint.length() == 0)
    {
        actionStatus = "fingerprint failed";
        return false;
    }

    Preferences preferences;
    if (!preferences.begin(RFID_PREFERENCES_NAMESPACE, false))
    {
        actionStatus = "NVS unavailable";
        return false;
    }

    const bool saved = preferences.putString("fp", fingerprint) > 0;
    preferences.end();

    if (!saved)
    {
        actionStatus = "save failed";
        return false;
    }

    savedFingerprint = fingerprint;
    actionStatus = "ID fingerprint saved";
    Serial.println("RFID identity fingerprint saved to NVS");
    return true;
}

void deleteSavedIdentity()
{
    Preferences preferences;

    if (!preferences.begin(RFID_PREFERENCES_NAMESPACE, false))
    {
        actionStatus = "NVS unavailable";
        return;
    }

    preferences.remove("fp");
    preferences.end();
    savedFingerprint = "";
    actionStatus = "saved ID deleted";
    Serial.println("RFID identity fingerprint deleted from NVS");
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
        actionStatus = "blocked: Classic only";
        lastToken = "unsupported type";
        return false;
    }

    MFRC522::StatusCode status = authenticateTokenBlock();
    if (status != MFRC522::STATUS_OK)
    {
        actionStatus = "blocked: default key failed";
        lastToken = "locked/non-Sentinel";
        reader.PCD_StopCrypto1();
        return false;
    }

    byte current[18];
    byte currentSize = sizeof(current);
    status = reader.MIFARE_Read(SENTINEL_TOKEN_BLOCK, current, &currentSize);

    if (status != MFRC522::STATUS_OK)
    {
        actionStatus = "blocked: cannot read block 4";
        reader.PCD_StopCrypto1();
        return false;
    }

    if (!isEmptyTokenBlock(current) && !hasSentinelPrefix(current))
    {
        actionStatus = "blocked: block 4 contains data";
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
        actionStatus = "write failed";
        reader.PCD_StopCrypto1();
        return false;
    }

    byte verify[18];
    byte verifySize = sizeof(verify);
    status = reader.MIFARE_Read(SENTINEL_TOKEN_BLOCK, verify, &verifySize);
    reader.PCD_StopCrypto1();

    if (status != MFRC522::STATUS_OK || memcmp(tokenData, verify, 16) != 0)
    {
        actionStatus = "verify failed";
        return false;
    }

    lastToken = generatedToken;
    actionStatus = "written + verified";
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
    display.setCursor(10, 45);
    display.print("RC522:");
    display.setCursor(65, 45);
    display.setTextColor(readerDetected ? ST77XX_GREEN : ST77XX_RED);
    display.print(readerDetected ? "READY" : "NOT FOUND");

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(180, 45);
    display.print("Ver:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(215, 45);

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
    display.setCursor(10, 63);
    display.print("UID:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(50, 63);
    display.print(lastUid.length() > 0 ? lastUid : "Present a tag");

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 81);
    display.print("Type:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(50, 81);
    display.print(lastType.length() > 0 ? lastType : "N/A");

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 99);
    display.print("Token:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(60, 99);
    display.print(lastToken);

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 117);
    display.print("State:");
    display.setTextColor(
        (writeArmed || verifyArmed) ? ST77XX_YELLOW : ST77XX_WHITE
    );
    display.setCursor(60, 117);

    if (!readerDetected)
        display.print("Check 3.3V, SPI, SS=7, RST=14");
    else if (writeArmed)
        display.print("WRITE ARMED - present OWN tag");
    else if (verifyArmed)
        display.print("VERIFY ARMED - present tag");
    else
        display.print(actionStatus);

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(10, 135);
    display.print("Reads:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(60, 135);
    display.print(readCount);

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(90, 135);
    display.print("Saved:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(135, 135);
    display.print(savedFingerprint.length() > 0 ? "YES" : "NO");

    display.setTextColor(ST77XX_GREEN);
    display.setCursor(190, 135);
    display.print("SD:");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(215, 135);
    display.print(isSDCardMounted() ? "OK" : "NO");

    for (uint8_t index = 0; index < RFID_ACTION_COUNT; index++)
    {
        const int16_t y = 145 + index * 10;

        if (index == selectedAction)
        {
            display.fillRoundRect(8, y - 1, 304, 10, 2, ST77XX_WHITE);
            display.setTextColor(ST77XX_BLACK);
        }
        else
        {
            display.setTextColor(ST77XX_WHITE);
        }

        display.setCursor(18, y);
        display.print(RFID_ACTION_LABELS[index]);
    }

    display.drawFastHLine(0, 216, 320, ST77XX_WHITE);
    display.setCursor(8, 225);
    display.print("UP");
    display.setCursor(145, 225);
    display.print("EXECUTE");
    display.setCursor(280, 225);
    display.print("DOWN");
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
    {
        readSentinelToken(type);

        if (verifyArmed)
        {
            verifyArmed = false;
            const String fingerprint = fingerprintUid(lastUid);
            const bool matches = fingerprint.length() > 0 &&
                                 fingerprint == savedFingerprint;
            actionStatus = matches ? "MATCH: saved identity" : "NO MATCH";
            Serial.println(
                matches ? "RFID identity matches saved fingerprint"
                        : "RFID identity does not match saved fingerprint"
            );
        }
    }

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
    verifyArmed = false;
    actionStatus = "safe read mode";
    selectedAction = 0;
    lastUid = "";
    lastType = "";
    lastToken = "none";
    lastReadMs = 0;
    loadSavedIdentity();
    initializeReader();
    drawReaderScreen();
}

void executeSelectedAction()
{
    const RfidAction action = static_cast<RfidAction>(selectedAction);

    switch (action)
    {
        case RfidAction::ReadTag:
            writeArmed = false;
            verifyArmed = false;
            actionStatus = "read mode: present a tag";
            lastReadMs = 0;
            drawReaderScreen();
            break;

        case RfidAction::SaveIdentity:
            writeArmed = false;
            verifyArmed = false;
            saveCurrentIdentity();
            drawReaderScreen();
            break;

        case RfidAction::VerifySaved:
            writeArmed = false;

            if (!readerDetected)
            {
                verifyArmed = false;
                actionStatus = "reader unavailable";
                drawReaderScreen();
                break;
            }

            if (savedFingerprint.length() == 0)
            {
                verifyArmed = false;
                actionStatus = "no saved ID";
                drawReaderScreen();
                break;
            }

            verifyArmed = true;
            verifyArmExpiresAt = millis() + VERIFY_ARM_WINDOW_MS;
            actionStatus = "verify armed for 10 seconds";
            lastReadMs = 0;
            drawReaderScreen();
            break;

        case RfidAction::WriteToken:
            verifyArmed = false;

            if (!readerDetected)
            {
                actionStatus = "reader unavailable";
                drawReaderScreen();
                break;
            }

            writeArmed = true;
            writeArmExpiresAt = millis() + WRITE_ARM_WINDOW_MS;
            actionStatus = "armed for 10 seconds";
            drawReaderScreen();
            break;

        case RfidAction::DeleteSaved:
            writeArmed = false;
            verifyArmed = false;
            deleteSavedIdentity();
            drawReaderScreen();
            break;

        case RfidAction::Reinitialize:
            writeArmed = false;
            verifyArmed = false;
            actionStatus = "reader reinitialized";
            initializeReader();
            drawReaderScreen();
            break;

        case RfidAction::Exit:
            writeArmed = false;
            verifyArmed = false;
            currentScreen = Screen::MainMenu;
            drawMenu();
            break;
    }
}

void handleRFID()
{
    if (writeArmed &&
        static_cast<int32_t>(millis() - writeArmExpiresAt) >= 0)
    {
        writeArmed = false;
        actionStatus = "arm expired; no write";
        drawReaderScreen();
    }

    if (verifyArmed &&
        static_cast<int32_t>(millis() - verifyArmExpiresAt) >= 0)
    {
        verifyArmed = false;
        actionStatus = "verify expired";
        drawReaderScreen();
    }

    if (pressed(BTN_TOP, lastTop))
    {
        if (selectedAction == 0)
            selectedAction = RFID_ACTION_COUNT - 1;
        else
            selectedAction--;

        drawReaderScreen();
        return;
    }

    if (pressed(BTN_MIDDLE, lastMiddle))
    {
        executeSelectedAction();
        return;
    }

    if (pressed(BTN_BOTTOM, lastBottom))
    {
        selectedAction = (selectedAction + 1) % RFID_ACTION_COUNT;
        drawReaderScreen();
        return;
    }

    pollReader();
}
