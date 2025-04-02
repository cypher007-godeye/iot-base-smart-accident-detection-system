#include <WiFi.h>
#include <Wire.h>
#include <MPU6050.h>
#include <TinyGPS++.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

const char* ssid = "ESP32WIFI";
const char* password = "123456789";
const char* botToken = "8075287655:AAHpI7fv0LWeAGz0RUtniyjaKIPHZKIeXZE";  // Replace with your bot token
const String chatId = "2122079402";     // Replace with your chat ID

MPU6050 mpu;
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);  // GPS on Serial2 (GPIO16, GPIO17)
AsyncWebServer server(80); // Web server on port 80
WiFiClientSecure client;
UniversalTelegramBot bot(botToken, client);

const float ACCIDENT_THRESHOLD = 2.5;
String latitude = "0.0", longitude = "0.0";
bool accidentDetected = false;

void setup() {
    Serial.begin(115200);
    Wire.begin();  // I2C (SDA = 21, SCL = 22)
    gpsSerial.begin(9600, SERIAL_8N1, 16, 17);

    mpu.initialize();
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
    Serial.println("\n✅ Connected to WiFi: " + WiFi.localIP().toString());

    client.setInsecure();  // For HTTPS connection with Telegram

    // Web server route for homepage
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", webpageHTML());
    });

    // Route to get sensor data (AJAX request)
    server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request){
        String json = "{\"acceleration\": " + String(getTotalAcceleration()) + 
                      ", \"latitude\": \"" + latitude + 
                      "\", \"longitude\": \"" + longitude + 
                      "\", \"accident\": " + (accidentDetected ? "true" : "false") + "}";
        request->send(200, "application/json", json);
    });

    server.begin(); // Start server
}

void loop() {
    updateSensorData();
    delay(500);
}

// Function to calculate total acceleration
float getTotalAcceleration() {
    int16_t ax, ay, az;
    mpu.getAcceleration(&ax, &ay, &az);
    float AccX = ax / 16384.0;
    float AccY = ay / 16384.0;
    float AccZ = az / 16384.0;
    return sqrt(AccX * AccX + AccY * AccY + AccZ * AccZ);
}

// Function to read GPS & detect accident
void updateSensorData() {
    while (gpsSerial.available()) {
        gps.encode(gpsSerial.read());
    }
    if (gps.location.isUpdated()) {
        latitude = String(gps.location.lat(), 6);
        longitude = String(gps.location.lng(), 6);
    }

    float acceleration = getTotalAcceleration();
    if (acceleration > ACCIDENT_THRESHOLD && !accidentDetected) {
        accidentDetected = true;
        sendTelegramAlert();
    } else if (acceleration <= ACCIDENT_THRESHOLD) {
        accidentDetected = false;
    }
}

// Function to send Telegram alert
void sendTelegramAlert() {
    String message = "🚨 Accident Detected!\n\n";
    message += "📍 Location: [" + latitude + ", " + longitude + "](https://www.google.com/maps?q=" + latitude + "," + longitude + ")\n";
    message += "💥 Impact Force: " + String(getTotalAcceleration(), 2) + " g";
    bot.sendMessage(chatId, message, "Markdown");
}

// HTML Webpage (AJAX Updates)
String webpageHTML() {
    return R"rawliteral(
       <!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Accident Detection</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        body {
            font-family: Arial, sans-serif;
            text-align: center;
            background-color: #f4f4f9;
            color: #333;
            margin: 0;
            padding: 20px;
            transition: background 0.3s;
        }
        .container {
            background: white;
            padding: 20px;
            border-radius: 10px;
            box-shadow: 0 4px 8px rgba(0, 0, 0, 0.2);
            display: inline-block;
            width: 80%;
        }
        h2 { color: #0056b3; }
        #status { font-size: 24px; font-weight: bold; }
        .map-container { width: 100%; height: 300px; margin-top: 10px; }
        .button { background: #007bff; color: white; padding: 10px 20px; border-radius: 5px; margin-top: 10px; }
        .log { text-align: left; max-height: 150px; overflow-y: auto; background: #eee; padding: 10px; border-radius: 5px; margin-top: 10px; }
        .dark-mode { background: #222; color: white; }
    </style>
</head>
<body>
    <div class="container">
        <h2>ESP32 Accident Detection System</h2>
        <button onclick="toggleDarkMode()">🌙 Toggle Dark Mode</button>
        <p><strong>Acceleration:</strong> <span id="acceleration">0.00</span> g</p>
        <p><strong>GPS Location:</strong> <span id="location">Waiting...</span></p>
        <iframe id="mapFrame" class="map-container"></iframe>
        <br>
        <a id="mapLink" class="button" href="#" target="_blank">📍 Open in Google Maps</a>
        <h3 id="status" style="color: green;">✅ Normal</h3>
        <canvas id="accelerationChart"></canvas>
        <div class="log" id="logContainer">
            <h4>Accident History</h4>
            <ul id="accidentLog"></ul>
        </div>
    </div>

    <script>
        let holdUpdate = false;
        let accelData = [];
        let labels = [];
        const maxDataPoints = 20;

        const ctx = document.getElementById('accelerationChart').getContext('2d');
        const chart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: labels,
                datasets: [{ label: 'Acceleration (g)', data: accelData, borderColor: 'red', borderWidth: 2, fill: false }]
            },
            options: { responsive: true, scales: { y: { beginAtZero: true } } }
        });

        function updateData() {
            if (holdUpdate) return;

            fetch('/data')
            .then(response => response.json())
            .then(data => {
                document.getElementById('acceleration').innerText = data.acceleration.toFixed(2);
                document.getElementById('location').innerText = data.latitude + ', ' + data.longitude;
                document.getElementById('mapFrame').src = "https://maps.google.com/maps?q=" + data.latitude + "," + data.longitude + "&output=embed";
                document.getElementById('mapLink').href = "https://www.google.com/maps?q=" + data.latitude + "," + data.longitude;
                
                labels.push(new Date().toLocaleTimeString());
                accelData.push(data.acceleration);
                if (labels.length > maxDataPoints) { labels.shift(); accelData.shift(); }
                chart.update();

                if (data.accident) {
                    document.getElementById('status').innerText = "🚨 Accident Detected!";
                    document.getElementById('status').style.color = "red";
                    holdUpdate = true;
                    setTimeout(() => { holdUpdate = false; }, 60000);
                    
                    const logEntry = document.createElement("li");
                    logEntry.innerText = Accident at ${data.latitude}, ${data.longitude} at ${new Date().toLocaleTimeString()};
                    document.getElementById('accidentLog').appendChild(logEntry);
                    
                    const audio = new Audio('https://www.soundjay.com/button/beep-07.wav');
                    audio.play();
                } else {
                    document.getElementById('status').innerText = "✅ Normal";
                    document.getElementById('status').style.color = "green";
                }
            });
        }
        setInterval(updateData, 1000);
        
        function toggleDarkMode() {
            document.body.classList.toggle("dark-mode");
        }
    </script>
</body>
</html>

    )rawliteral";
}