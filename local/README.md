# Локальные Wi-Fi-данные

`confirmed_wifi_credentials.csv` предназначен только для учётных данных,
которые владелец сети уже подтвердил или ввёл сам. Это обычный незашифрованный
CSV-файл; не синхронизируйте и не публикуйте его. Файл исключён из Git.

Управлять локальной конфигурацией можно без ручного редактирования C++:

```bash
cd /Users/maksimkureev/Documents/PlatformIO/Projects/SentinelFirmware
python3 tools/wifi_credentials.py configure
python3 tools/wifi_credentials.py record
python3 tools/wifi_credentials.py list
```

`configure` обновляет `include/wifi_credentials.h`. `record` добавляет или
обновляет подтверждённую запись в CSV. Пароль запрашивается скрыто и не
передаётся аргументом командной строки.
