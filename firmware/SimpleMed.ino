#include <WiFi.h>

#include <WebServer.h>

#include <DNSServer.h>

#include <RTClib.h>

#include <ESP32Servo.h>

#include <WiFiClientSecure.h>

#include <UniversalTelegramBot.h>

#include <Preferences.h>

#include <esp_system.h>

#include <time.h>

#include "soc/soc.h"

#include "soc/rtc_cntl_reg.h"



RTC_DS3231 rtc;

bool rtc_ok = false;



Servo servo1;

Servo servo2;



WebServer server(80);

DNSServer dnsServer;

const byte DNS_PORT = 53;



Preferences prefs;



int slot1_hour = 0, slot1_minute = 0;

int slot2_hour = 0, slot2_minute = 0;

bool slot1_configured = false;

bool slot2_configured = false;



String home_ssid = "";

String home_pass = "";

String telegram_chat_id = "";

bool telegram_active = false;




int duration_days = 0;

int elapsed_days = 0;

int last_seen_day = -1; 




String lang = "en";



#ifndef BOT_TOKEN

#define BOT_TOKEN "YOUR_TELEGRAM_BOT_TOKEN_HERE"

#endif

const char* botToken = BOT_TOKEN;



WiFiClientSecure secured_client;

UniversalTelegramBot bot(botToken, secured_client);



#define BUZZER 5

#define LED    2

#define BUTTON 15



bool slot1_alarm  = false;

bool slot2_alarm  = false;

bool slot1_taken = false;

bool slot2_taken = false;



unsigned long lastCheck = 0;

unsigned long slot1_open_time = 0;

unsigned long slot2_open_time = 0;



bool slot1_lid_open_waiting = false;

bool slot2_lid_open_waiting = false;



bool setup_phase_active = true;

bool lids_opened_for_setup = false;



bool internet_connect_request = false;

bool wifi_connecting = false;

int wifi_connect_attempts = 0;

const int WIFI_MAX_ATTEMPTS = 240; 

const unsigned long WIFI_RETRY_STEP_MS = 500;

unsigned long lastWifiAttemptTick = 0;



bool pending_connect_message = false;

bool pending_power_loss_message = false;

bool pending_ready_message = false;

bool pending_finished_message = false;




volatile bool buttonPressedFlag = false;

unsigned long lastInterruptTime = 0;



void IRAM_ATTR buttonISR() {

  unsigned long interruptTime = millis();

  if (interruptTime - lastInterruptTime > 250) {

    buttonPressedFlag = true;

    lastInterruptTime = interruptTime;

  }

}



// Buzzer 

bool buzzer_active = false;

unsigned long lastBuzzerToggle = 0;

const unsigned long BUZZER_ON_MS  = 300;

const unsigned long BUZZER_OFF_MS = 200;

bool buzzer_pin_state = false;



void updateBuzzer() {

  if (!buzzer_active) {

    digitalWrite(BUZZER, LOW);

    buzzer_pin_state = false;

    return;

  }

  unsigned long now = millis();

  unsigned long currentInterval = buzzer_pin_state ? BUZZER_ON_MS : BUZZER_OFF_MS;

  if (now - lastBuzzerToggle >= currentInterval) {

    buzzer_pin_state = !buzzer_pin_state;

    digitalWrite(BUZZER, buzzer_pin_state ? HIGH : LOW);

    lastBuzzerToggle = now;

  }

}



void startBuzzer() {

  if (!buzzer_active) {

    buzzer_active = true;

    buzzer_pin_state = true;

    digitalWrite(BUZZER, HIGH);

    lastBuzzerToggle = millis();

  }

}



void stopBuzzerHard() {

  buzzer_active = false;

  buzzer_pin_state = false;

  digitalWrite(BUZZER, LOW);

}



//  "biip"

void startupBeep() {

  digitalWrite(BUZZER, HIGH);

  delay(150);

  digitalWrite(BUZZER, LOW);

}





String tr_ready(int h1, int m1, int h2, int m2, int days) {

  char buf1[6], buf2[6];

  sprintf(buf1, "%02d:%02d", h1, m1);

  sprintf(buf2, "%02d:%02d", h2, m2);

  String daysTxt;

  if (lang == "ka") {

    daysTxt = (days > 0) ? (String(days) + " დღე") : "განუსაზღვრელი ვადით";

    return "✅ Smart Pill Box მზადაა!\nსლოტი 1: " + String(buf1) + "\nსლოტი 2: " + String(buf2) + "\nხანგრძლივობა: " + daysTxt;

  }

  daysTxt = (days > 0) ? (String(days) + " days") : "unlimited";

  return "✅ Smart Pill Box is ready!\nSlot 1: " + String(buf1) + "\nSlot 2: " + String(buf2) + "\nDuration: " + daysTxt;

}



String tr_connected() {

  if (lang == "ka") return "🚀 Smart Pill Box ინტერნეტს დაუკავშირდა!";

  return "🚀 Smart Pill Box connected to the internet!";

}



String tr_taken(int slot) {

  if (lang == "ka") return "✅ მედიკამენტი მიღებულია: სლოტი " + String(slot) + ".";

  return "✅ Medication taken: Slot " + String(slot) + ".";

}



String tr_alert(int slot) {

  if (lang == "ka") return "🚨 გაფრთხილება: სლოტი " + String(slot) + " დროა!";

  return "🚨 ALERT: Slot " + String(slot) + " medication time has arrived!";

}



String tr_power_loss() {

  if (lang == "ka") return "⚠️ ყურადღება: შესაძლოა მოხდა დენის გათიშვა ან ვარდნა. მოწყობილობა გადაიტვირთა.";

  return "⚠️ Warning: A power interruption or voltage drop may have occurred. The device restarted.";

}



String tr_finished() {

  if (lang == "ka") return "🏁 მკურნალობის კურსი დასრულდა. შეხსენებები გაჩერდა.";

  return "🏁 The medication course has finished. Reminders have stopped.";

}



//  Web 

void handleRoot() {

  Serial.println("[SYSTEM] Sending web interface.");




  if (!lids_opened_for_setup) {

    lids_opened_for_setup = true;

    

    Serial.println("[KAPAK] Arayuze baglanildi. Servolar sirayla aciliyor...");

    servo1.attach(18, 500, 2400); 

    servo1.write(50); 

    delay(400); 

    

    servo2.attach(19, 500, 2400); 

    servo2.write(50); 

  }



  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";

  html += "<style>body{font-family:Arial; text-align:center; background:#f4f4f9; padding:15px;} .container{background:white; padding:20px; border-radius:10px; max-width:420px; margin:auto; text-align:left;} input,select{font-size:16px; padding:10px; margin:8px 0; width:95%;} button{background:#4CAF50; color:white; padding:14px; border:none; border-radius:5px; width:100%; font-weight:bold; cursor:pointer;}</style>";

  html += "<script>function sendTime(){ var d = new Date(); document.getElementById('phone_time').value = d.getHours() + ':' + d.getMinutes() + ':' + d.getSeconds(); }</script></head>";

  html += "<body><div class='container'><h2>Smart Pill Box</h2><form action='/save' method='GET' onsubmit='sendTime()'>";

  html += "<h4>Language / ენა</h4><select name='lang'><option value='en'>English</option><option value='ka'>ქართული</option></select><br>";

  html += "<h4>1. Alarm Times / დროები</h4><label>Slot 1:</label><input type='time' name='b1' required><br><label>Slot 2:</label><input type='time' name='b2' required><br>";

  html += "<h4>2. Duration / ხანგრძლივობა</h4><label>Days (0 = unlimited / შეუზღუდავი):</label><input type='number' name='days' min='0' value='0'><br>";

  html += "<h4>3. Wifi & Telegram (Optional)</h4><label>SSID:</label><input type='text' name='wifi_ssid'><br><label>Password:</label><input type='password' name='wifi_pass'><br><label>Telegram Chat ID:</label><input type='text' name='chat_id'><br>";

  html += "<input type='hidden' id='phone_time' name='t_time'><button type='submit'>Save Settings / შენახვა</button></form></div></body></html>";

  server.send(200, "text/html; charset=UTF-8", html);

}



void handleSave() {

  if (server.hasArg("b1") && server.hasArg("b2")) {



    if (server.hasArg("lang")) {

      lang = server.arg("lang");

      if (lang != "ka") lang = "en";

      prefs.putString("lang", lang);

    }



    slot1_hour = server.arg("b1").substring(0, 2).toInt();

    slot1_minute = server.arg("b1").substring(3, 5).toInt();

    slot1_configured = true; slot1_taken = false;



    slot2_hour = server.arg("b2").substring(0, 2).toInt();

    slot2_minute = server.arg("b2").substring(3, 5).toInt();

    slot2_configured = true; slot2_taken = false;



    duration_days = server.hasArg("days") ? server.arg("days").toInt() : 0;

    if (duration_days < 0) duration_days = 0;

    elapsed_days = 0;

    last_seen_day = -1;




    servo1.write(5); delay(300); servo2.write(5);

    setup_phase_active = false;



    if (server.hasArg("t_time") && rtc_ok) {

      String t_time = server.arg("t_time");

      int t_h = t_time.substring(0, t_time.indexOf(':')).toInt();

      int t_m = t_time.substring(t_time.indexOf(':') + 1, t_time.lastIndexOf(':')).toInt();

      int t_s = t_time.substring(t_time.lastIndexOf(':') + 1).toInt();

      DateTime now = rtc.now();

      rtc.adjust(DateTime(now.year(), now.month(), now.day(), t_h, t_m, t_s));

      last_seen_day = now.day();

    }



    home_ssid = server.arg("wifi_ssid");

    home_pass = server.arg("wifi_pass");

    telegram_chat_id = server.arg("chat_id");



    prefs.putInt("s1h", slot1_hour); prefs.putInt("s1m", slot1_minute);

    prefs.putInt("s2h", slot2_hour); prefs.putInt("s2m", slot2_minute);

    prefs.putBool("s1cfg", true); prefs.putBool("s2cfg", true);

    prefs.putInt("durdays", duration_days);

    prefs.putInt("elapdays", elapsed_days);

    prefs.putInt("lastday", last_seen_day);

    prefs.putString("ssid", home_ssid);

    prefs.putString("pass", home_pass);

    prefs.putString("chatid", telegram_chat_id);



    String msg = "";

    if (home_ssid.length() > 0 && telegram_chat_id.length() > 0) {

      telegram_active = true;

      prefs.putBool("tg_on", true);

      internet_connect_request = true;

      pending_ready_message = true;

      msg = (lang == "ka") ? "<p>ინტერნეტთან დაკავშირება...</p>" : "<p>Connecting to internet...</p>";

    } else {

      telegram_active = false;

      prefs.putBool("tg_on", false);

      msg = (lang == "ka") ? "<p>ჩართულია <b>ლოკალურ რეჟიმში</b>.</p>" : "<p>Started in <b>Local Mode</b>.</p>";

    }



    String response = "<html><body style='text-align:center; font-family:Arial; margin-top:50px;'><h2>✅ Saved!</h2>" + msg + "</body></html>";

    server.send(200, "text/html; charset=UTF-8", response);

  }

}



bool sendTelegramReliable(const String &message) {

  if (!telegram_active || WiFi.status() != WL_CONNECTED) {

    Serial.println("[TELEGRAM] Aborted: Wifi not connected or Telegram inactive.");

    return false;

  }



  digitalWrite(BUZZER, LOW);

  Serial.print("[TELEGRAM] Trying to send: "); Serial.println(message);

  bool ok = bot.sendMessage(telegram_chat_id, message, "");



  if (!ok) {

    Serial.println("[TELEGRAM] First attempt failed. Retrying in 1 second...");

    delay(1000);

    ok = bot.sendMessage(telegram_chat_id, message, "");

  }



  if (ok) {

    Serial.println("[TELEGRAM] Sent successfully!");

  } else {

    Serial.println("[TELEGRAM] ERROR: Could not send message.");

  }



  lastBuzzerToggle = millis();

  return ok;

}



void setup() {

  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);



  Serial.begin(115200);

  delay(500);

  Serial.println("\n=== SYSTEM STARTING ===");



  esp_reset_reason_t reason = esp_reset_reason();

  bool unexpected_restart = (reason != ESP_RST_POWERON && reason != ESP_RST_SW);



  pinMode(BUZZER, OUTPUT);

  pinMode(LED, OUTPUT);



  pinMode(BUTTON, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(BUTTON), buttonISR, FALLING);



  digitalWrite(BUZZER, LOW);

  digitalWrite(LED, LOW);




  startupBeep();



  prefs.begin("pillbox", false);

  lang = prefs.getString("lang", "en");

  slot1_hour = prefs.getInt("s1h", 0);

  slot1_minute = prefs.getInt("s1m", 0);

  slot2_hour = prefs.getInt("s2h", 0);

  slot2_minute = prefs.getInt("s2m", 0);

  slot1_configured = prefs.getBool("s1cfg", false);

  slot2_configured = prefs.getBool("s2cfg", false);

  duration_days = prefs.getInt("durdays", 0);

  elapsed_days = prefs.getInt("elapdays", 0);

  last_seen_day = prefs.getInt("lastday", -1);

  home_ssid = prefs.getString("ssid", "");

  home_pass = prefs.getString("pass", "");

  telegram_chat_id = prefs.getString("chatid", "");

  telegram_active = prefs.getBool("tg_on", false);



  if (slot1_configured || slot2_configured) {

    setup_phase_active = false; 


    servo1.attach(18, 500, 2400); servo1.write(5); delay(150);

    servo2.attach(19, 500, 2400); servo2.write(5);

  }



  IPAddress local_IP(192, 168, 4, 1);

  IPAddress gateway(192, 168, 4, 1);

  IPAddress subnet(255, 255, 255, 0);



  WiFi.mode(WIFI_AP);

  WiFi.softAPConfig(local_IP, gateway, subnet);

  WiFi.softAP("SmartPillBox-Setup");



  dnsServer.start(DNS_PORT, "*", local_IP);

  server.on("/", handleRoot);

  server.on("/save", handleSave);

  server.onNotFound(handleRoot);

  server.begin();



  secured_client.setInsecure();



  if (!rtc.begin()) {

    Serial.println("[ERROR] RTC module not found!");

    rtc_ok = false;

  } else {

    rtc_ok = true;

  }



  if (unexpected_restart && telegram_active && home_ssid.length() > 0) {

    Serial.println("[SYSTEM] Unexpected restart detected.");

    internet_connect_request = true;

    pending_power_loss_message = true;

  }

}



void loop() {

  dnsServer.processNextRequest();

  server.handleClient();

  updateBuzzer();



  if (internet_connect_request) {

    internet_connect_request = false;

    Serial.println("[WIFI] Connecting to router...");

    WiFi.mode(WIFI_AP_STA);

    WiFi.begin(home_ssid.c_str(), home_pass.c_str());

    wifi_connecting = true;

    wifi_connect_attempts = 0;

    lastWifiAttemptTick = millis();

  }



  if (wifi_connecting) {

    if (WiFi.status() == WL_CONNECTED) {

      wifi_connecting = false;

      Serial.println("[SYSTEM] Wifi Connected. Syncing time...");

      configTime(0, 0, "pool.ntp.org", "time.nist.gov");



      time_t now = time(nullptr);

      int timeout_cnt = 0;

      while (now < 24 * 3600 && timeout_cnt < 20) {

        delay(500);

        now = time(nullptr);

        timeout_cnt++;

      }

      pending_connect_message = true;

    } else if (millis() - lastWifiAttemptTick >= WIFI_RETRY_STEP_MS) {

      lastWifiAttemptTick = millis();

      wifi_connect_attempts++;

      if (wifi_connect_attempts >= WIFI_MAX_ATTEMPTS) {

        wifi_connecting = false;

        WiFi.mode(WIFI_AP);

        Serial.println("[WIFI] Connection Timeout.");

      }

    }

  }



  if (pending_connect_message && WiFi.status() == WL_CONNECTED) {

    pending_connect_message = false;

    sendTelegramReliable(tr_connected());

  }



  if (pending_ready_message && WiFi.status() == WL_CONNECTED) {

    pending_ready_message = false;

    sendTelegramReliable(tr_ready(slot1_hour, slot1_minute, slot2_hour, slot2_minute, duration_days));

  }



  if (pending_power_loss_message && WiFi.status() == WL_CONNECTED) {

    pending_power_loss_message = false;

    sendTelegramReliable(tr_power_loss());

  }



  if (pending_finished_message && WiFi.status() == WL_CONNECTED) {

    pending_finished_message = false;

    sendTelegramReliable(tr_finished());

  }




  if (buttonPressedFlag) {

    buttonPressedFlag = false;

    Serial.println("[BUTTON] Click processed.");



    if (slot1_alarm && !slot1_taken) {

      stopBuzzerHard();

      servo1.write(50);

      slot1_lid_open_waiting = true;

      slot1_open_time = millis();

      slot1_alarm = false;

      slot1_taken = true;

      sendTelegramReliable(tr_taken(1));

    }



    if (slot2_alarm && !slot2_taken) {

      stopBuzzerHard();

      servo2.write(55); 

      slot2_lid_open_waiting = true;

      slot2_open_time = millis();

      slot2_alarm = false;

      slot2_taken = true;

      sendTelegramReliable(tr_taken(2));

    }



    if (!slot1_alarm && !slot2_alarm) {

      stopBuzzerHard();

      digitalWrite(LED, LOW);

    }

  }




  if (slot1_lid_open_waiting && (millis() - slot1_open_time >= 120000)) {

    servo1.write(5);

    slot1_lid_open_waiting = false;

  }

  if (slot2_lid_open_waiting && (millis() - slot2_open_time >= 120000)) {

    servo2.write(5);

    slot2_lid_open_waiting = false;

  }




  if (rtc_ok && (millis() - lastCheck >= 1000)) {

    lastCheck = millis();

    DateTime now = rtc.now();



    if (slot1_configured && now.hour() == slot1_hour && now.minute() == slot1_minute && !slot1_alarm && !slot1_taken) {

      slot1_alarm = true;

      digitalWrite(LED, HIGH);

      startBuzzer();

      sendTelegramReliable(tr_alert(1));

    }



    if (slot2_configured && now.hour() == slot2_hour && now.minute() == slot2_minute && !slot2_alarm && !slot2_taken) {

      slot2_alarm = true;

      digitalWrite(LED, HIGH);

      startBuzzer();

      sendTelegramReliable(tr_alert(2));

    }



    if (last_seen_day != now.day()) {

      if (last_seen_day != -1) {

        elapsed_days++;

        prefs.putInt("elapdays", elapsed_days);



        if (duration_days > 0 && elapsed_days >= duration_days) {

          slot1_configured = false;

          slot2_configured = false;

          prefs.putBool("s1cfg", false);

          prefs.putBool("s2cfg", false);

          pending_finished_message = true;

        }

      }

      last_seen_day = now.day();

      prefs.putInt("lastday", last_seen_day);



      slot1_alarm = false; slot2_alarm = false;

      slot1_taken = false; slot2_taken = false;

    }

  }

}
