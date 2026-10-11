#pragma once

#include <SentinelWiFiGuard.h>

void initializeStorage();
bool isSDCardMounted();
bool logRFIDRead(const String& uid, const String& type);
void logWiFiGuardSnapshot(const SentinelWiFiGuardSnapshot& snapshot);
