/*1.Подключение к WiFi : статическое, 
    проверка подключения, переподключение через интервал
  2.Проверка обновлений OTA : при запуске системы, 
    при переподключении к сети WiFi, через установленный интервал
    обновление определяется по версии и должно быть старше текущей 
  3.Скачивание производится с репозитория Github файл firmwareList.txt
  4.Выводится информация о разделах текущий и обновляемый Serial port
  5.
*/
#include <WiFi.h>
//#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoOTA.h> //обновление по воздуху
#include <SPIFFS.h> //файловая система
#include <esp_ota_ops.h> //чтение информации о разделах
#include <ESPAsyncWebServer.h>


//глобальные переменные firmware
const String firmwareVersion = "1.0.3";
const String firmwareListURL = "https://raw.githubusercontent.com/CosmoCraic/test/main/firmwareList.txt";
bool isUpdating = false;
bool isUpdateAvailable = false;
bool isDownloading = false;
bool isInstalling = false;
String newFirmwareURL = ""; //хранение ссылки на новую версию прошивки

const long updateInterval = 15 * 60 * 1000; // X минут в миллисекундах
unsigned long lastUpdateTime = 0;
//bool lowActivityUpdate = false; //включение опции поиска и установки обновления в период низкой активности

//глобальные переменные WiFi
const char* ssid = "RN10Pro";
const char* password = "es123123";
bool isConnected = false;
unsigned long lastAttemptTime = 0;
const long reconnectInterval = 10000; // 10 секунд между попытками

//web server и html
AsyncWebServer server(80); // Используем порт 80 для HTTP


void setup() {
  Serial.begin(115200);
  if (!SPIFFS.begin(true)) {
     Serial.println("Ошибка инициализации SPIFFS");
     return;
  }
  setupWiFi();
  printPartitionInfo();
  setupWebServer();// Настройка сервера и маршрутов
  server.begin();// Запуск сервера

}

void loop() {
  checkWiFiConnection();
  firmwareStatus();
  
    
    // Регулярная проверка обновлений
  if (WiFi.status() == WL_CONNECTED && millis() - lastUpdateTime > updateInterval) {
    checkForUpdates();
    lastUpdateTime = millis(); // Обновить время последней проверки
  }

  ArduinoOTA.handle();
}
//_________________________________________
//подключение wifi начало
//_________________________________________
void setupWiFi() {
  WiFi.begin(ssid, password);
  Serial.println("Пытаемся подключиться к WiFi...");

  unsigned long startAttemptTime = millis();
  
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 5000) {
    // Просто ожидаем
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Не удалось подключиться к WiFi в течение 5 секунд. Продолжаем...");
  } else {
    Serial.println("Подключено к WiFi!");
    Serial.print("IP-Адрес: ");
    Serial.println(WiFi.localIP());
    isConnected = true;
  }
}

void checkWiFiConnection() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!isConnected) {
      Serial.println("Подключено к WiFi");
      Serial.print("IP-Адрес: ");
      Serial.println(WiFi.localIP());
      isConnected = true;
       // Проверка обновлений при каждом успешном подключении к WiFi
      //checkForUpdates();
    }
  } else {
    isConnected = false;
    unsigned long currentTime = millis();
    if (!isConnected && currentTime - lastAttemptTime > reconnectInterval) {
      Serial.println("Подключение к WiFi потеряно. Пытаемся переподключиться...");
      WiFi.disconnect();
      WiFi.reconnect();
      lastAttemptTime = currentTime;
    }
  }
}
//_________________________________________
//подключение wifi конец
//_________________________________________


//_________________________________________
//обновление прошивки начало
//_________________________________________


//____функция проверки флагов и состояний обновления________
void firmwareStatus() {
    if (isUpdating) {
        String firmwareURL = checkForUpdates();
        if (!firmwareURL.isEmpty()) {
            isUpdateAvailable = true;
            newFirmwareURL = firmwareURL; // Сохраняем URL для загрузки
        } else {
            Serial.println("Обновлений нет или ошибка при проверке.");
        }
        isUpdating = false;
    }

    if (isUpdateAvailable && !isDownloading) {
        if (downloadAndUpdate(newFirmwareURL)) {
            isDownloading = true;
        } else {
            Serial.println("Ошибка при загрузке обновления.");
        }
        isUpdateAvailable = false;
    }

    if (isDownloading && !isInstalling) {
        if (performUpdate()) {
            isInstalling = true;
        } else {
            Serial.println("Ошибка при установке обновления.");
        }
        isDownloading = false;
    }
}


//_______Функция проверки наличия новой версии прошивки__________

String checkForUpdates() {
    Serial.println("Проверка новой версии прошивки. Скачиваем файл firmwareList...");
    HTTPClient http;
    http.begin(firmwareListURL);
    int httpCode = http.GET();

    if (httpCode == 200) {
        String payload = http.getString();
        String newFirmwareURL = parseFirmwareList(payload, firmwareVersion);
        http.end();
        if (!newFirmwareURL.isEmpty()) {
            return newFirmwareURL;
        } else {
            Serial.println("Нет доступных обновлений.");
        }
    } else {
        Serial.println("Ошибка HTTP: " + http.errorToString(httpCode));
    }
    http.end();
    return "";
}

String parseFirmwareList(const String& payload, const String& currentVersion) {
    String latest_version_url = "";
    String latest_version = "0.0.0"; // Начальное значение для сравнения версий
    int line_start = 0;
    int line_end = payload.indexOf('\n', line_start);

    while (line_start < payload.length() && line_end != -1) {
        String line = payload.substring(line_start, line_end);
        line.trim(); // Удаление пробелов с обеих сторон строки

        int separator_pos = line.indexOf(';');
        if (separator_pos != -1) {
            String version = line.substring(0, separator_pos);
            String url = line.substring(separator_pos + 1);

            if (is_newer_version(version, currentVersion) && is_newer_version(version, latest_version)) {
                latest_version = version;
                latest_version_url = url;
            }
        }

        line_start = line_end + 1;
        line_end = payload.indexOf('\n', line_start);
    }

    return latest_version_url;
}

bool is_newer_version(const String& new_version, const String& current_version) {
    int new_major, new_minor, new_patch;
    int current_major, current_minor, current_patch;

    sscanf(new_version.c_str(), "%d.%d.%d", &new_major, &new_minor, &new_patch);
    sscanf(current_version.c_str(), "%d.%d.%d", &current_major, &current_minor, &current_patch);

    if (new_major > current_major) return true;
    if (new_major < current_major) return false;
    if (new_minor > current_minor) return true;
    if (new_minor < current_minor) return false;
    return new_patch > current_patch;
}


bool downloadAndUpdate(const String& url) {
    WiFiClientSecure client;
    client.setInsecure(); // Используйте эту опцию, если сертификат SSL не может быть проверен

    Serial.println("Начинаем загрузку прошивки с URL: " + url);

    HTTPClient http;
    http.begin(client, url);
    Serial.print("[HTTP] Начинаем... ");
    Serial.println(url);
  
    int httpCode = http.GET();

    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_FOUND || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
            String newUrl = http.getLocation();
            http.end();
            http.begin(client, newUrl);
            httpCode = http.GET();
        }

        if (httpCode == HTTP_CODE_OK) {
            WiFiClient * stream = http.getStreamPtr();
            File updateFile = SPIFFS.open("/update.bin", "w");
            if (!updateFile) {
                Serial.println("Не удалось открыть файл для записи");
                http.end();
                return false;
            }

            const size_t bufferSize = 1024;
            uint8_t buffer[bufferSize];
            int bytesRead;
            int totalBytes = 0;

            while ((bytesRead = stream->readBytes(buffer, bufferSize)) > 0) {
                updateFile.write(buffer, bytesRead);
                totalBytes += bytesRead;
                Serial.print(".");
            }
            updateFile.close();
            Serial.println("\nЗагрузка завершена. Записано байт: " + String(totalBytes));
            http.end();
            return true;
        } else {
            Serial.println("Ошибка HTTP при загрузке файла: " + http.errorToString(httpCode));
        }
    } else {
        Serial.println("Ошибка соединения: " + http.errorToString(httpCode));
    }
    http.end();
    return false;
}


//функция установки скачанной прошивки
bool performUpdate() {
    File updateFile = SPIFFS.open("/update.bin", "r");
    if (!updateFile) {
        Serial.println("Не удалось открыть файл обновления");
        return false;
    }

    if (Update.begin(updateFile.size())) {
        size_t written = Update.writeStream(updateFile);
        if (written == updateFile.size()) {
            Serial.println("Обновление завершено");
            if (Update.end(true)) {
                Serial.println("Обновление успешно установлено!");
                ESP.restart();
                return true;
            } else {
                Serial.println("Ошибка завершения обновления: " + String(Update.getError()));
            }
        } else {
            Serial.println("Ошибка записи обновления");
        }
        updateFile.close();
    } else {
        Serial.println("Недостаточно места для обновления");
    }
    return false;
}


//_________________________________________
//обновление прошивки конец
//_________________________________________


//_________________________________________
//работа с разделами начало
//_________________________________________
//вывод информации о разделах
void printPartitionInfo() {
  Serial.println("Информация о разделах:");

  // Перебор и вывод всех разделов приложения
  esp_partition_iterator_t iter = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
  while (iter != NULL) {
    const esp_partition_t *partition = esp_partition_get(iter);
    Serial.print("Раздел: ");
    Serial.print(partition->label);
    Serial.print(", Размер: ");
    Serial.print(partition->size);
    Serial.print(", Адрес: ");
    Serial.println(partition->address, HEX);
    iter = esp_partition_next(iter);
  }
  esp_partition_iterator_release(iter);

  // Получение информации о текущем исполняемом разделе
  const esp_partition_t *running_partition = esp_ota_get_running_partition();
  Serial.print("Текущий исполняемый раздел: ");
  Serial.println(running_partition->label);

  // Получение информации о следующем обновляемом разделе
  const esp_partition_t *next_update_partition = esp_ota_get_next_update_partition(NULL);
  Serial.print("Следующий обновляемый раздел: ");
  Serial.println(next_update_partition ? next_update_partition->label : "нет");
}

//_________________________________________
//работа с разделами конец
//_________________________________________

//_________________________________________
//работа с web сервером начало
//_________________________________________

String processor(const String& var){
  if(var == "FIRMWARE_VERSION"){
    return firmwareVersion;
  }
  return String();
}

//_________________________________________

//HTML страница
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Web Server</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 0; padding: 0; }
        .container { padding: 20px; }
        #firmwareVersion { color: green; }
        .button { background-color: #4CAF50; color: white; padding: 15px 20px; border: none; border-radius: 4px; cursor: pointer; }
        .button:hover { background-color: #45a049; }
    </style>
</head>
<body>
    <div class="container">
        <h1>ESP32 Web Server</h1>
        <p>Текущая версия прошивки: <span id="firmwareVersion"></span></p>
        <button class="button" onclick="checkForUpdates()">Проверить обновления</button>
        <p id="updateResult"></p>
    </div>

    <script>
        document.getElementById('firmwareVersion').innerText = '%FIRMWARE_VERSION%';

        function checkForUpdates() {
            document.getElementById('updateResult').innerText = 'Проверка обновлений...';
            // Отправляем запрос на ESP32 для проверки обновлений
            fetch('/update-check')
                .then(response => response.text())
                .then((data) => {
                    document.getElementById('updateResult').innerText = data;
                });
            }
        </script>
    </body>
  </html>
)rawliteral";

//настройка маршрутов
void setupWebServer() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html, processor);
    });

    server.on("/update-check", HTTP_GET, [](AsyncWebServerRequest *request){
        isUpdating = true; // Устанавливаем флаг для начала процесса проверки обновлений
        String responseMessage;
        if (checkForUpdates()) {
            responseMessage = "Доступно обновление. Версия: " + newFirmwareURL; // или любая другая информация о версии
        } else {
            responseMessage = "Обновлений нет или произошла ошибка.";
        }
        request->send(200, "text/plain", responseMessage);
    });

    // Дополнительные маршруты можно добавить здесь
}

//_________________________________________
//работа с web сервером конец
//_________________________________________
