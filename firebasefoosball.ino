// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <Adafruit_DotStar.h>
#include <Bridge.h>
#include <Process.h>
#include <HttpClient.h>
#include "U8glib.h"
#include <elapsedMillis.h>

#define       PIN_DATA1             4
#define       PIN_CLK1              5
#define       PIN_DATA2             6
#define       PIN_CLK2              7
#define       NUM_BUTTONS           4
#define       NUM_PIXELS            10
#define       INTERVAL_CHECKNETWORK 10000
#define       INTERVAL_ANIMATION    20
#define       INTERVAL_CHECKSCORE   1000


String        DATA_PAYLOAD        = "{\".sv\": \"timestamp\"}";
// String        DATA_AUTH         = "?auth=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJleHAiOjE0NjU2NjMwMDksInYiOjAsImQiOnsidWlkIjoiYmFuYW5hIn0sImlhdCI6MTQ2MzA3MTAwOX0.qEaQ0mSFd48a_NiUIX53OI6PuLKOOFRvJ9akcBMEKH4";
String DATA_AUTH = "";
String        DATA_NAME           = "https://functions-prerelease-11-bb35b.firebaseio.com";
String        MSG_BANNER          = "fireboy";
int           game_started        = 0;
int           score1              = -1;
int           score2              = -1;
int           button_pins[]       = {A0, A1, A2, A3};
const char*   button_names[]      = {"B0", "B1", "B2", "B3"};
int           button_values[]     = {HIGH, HIGH, HIGH, HIGH};
elapsedMillis network_timer;
Process       game_proc;
Process       score_proc;
String        board_id;
String        status_message      = "starting";
String        ip_message          = "";

U8GLIB_SH1106_128X64 u8g(U8G_I2C_OPT_DEV_0|U8G_I2C_OPT_FAST);
Adafruit_DotStar pixel_strip1 = Adafruit_DotStar(NUM_PIXELS, PIN_DATA1, PIN_CLK1, DOTSTAR_BGR);
Adafruit_DotStar pixel_strip2 = Adafruit_DotStar(NUM_PIXELS, PIN_DATA2, PIN_CLK2, DOTSTAR_BGR);

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  Serial.println("setup starting.");
  Bridge.begin();  
  for (int i = 0; i < NUM_BUTTONS; i++) {
    pinMode(button_pins[i], INPUT_PULLUP);
  }
  pixel_strip1.begin();
  pixel_strip2.begin();
  pixel_strip1.show();
  pixel_strip2.show();
  Serial.println("setup complete.");
  listen_table();
  animate_strips(0x00FF00);
  animate_strips(0xFF0000);
  animate_strips(0x0000FF);
}

void loop() {
  iter_buttons();
  iter_display();
  iter_processes();
}

void iter_buttons() {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    int val = digitalRead(button_pins[i]);
    if (val != button_values[i]) {
      button_values[i] = val;
      if (val == LOW) {
        Serial.println("click" + i);
        send_button_click(i);
      }
    }
  }
}

void iter_display() {
  u8g.firstPage();  
  do {
    update_display();
  } while( u8g.nextPage() );
}

void iter_processes() {
  if (game_proc.available()) {
    String s = game_proc.readStringUntil('\n');
    Serial.println("game:" + s);
    // data: {"path":"/","data":"/games/-KH2s6AVIfHOwixsDQuF"}
    if (s.indexOf("/games/") > -1) {
      String game_id = s.substring(26, s.length() - 2);
      Serial.println("gameid:" + game_id + "|");
      listen_game(game_id);
      game_started = 1;
    }
  }
  if (game_started && score_proc.available()) {
    String s = score_proc.readStringUntil('\n');
    Serial.println("score|" + s + "|");
    if (s.indexOf("path") > -1 && s.indexOf("_score") > -1) {
      String data = s.substring(6);
      Serial.println("data|" + data + "|");
      if (s.indexOf("team_1_score") > -1 && s.indexOf("team_2_score") > -1) {
        // This is the whole game object
        String t1 = s.substring(s.indexOf("team_1_score") + 14);
        String sc1 = t1.substring(0, t1.indexOf(","));
        String t2 = s.substring(s.indexOf("team_2_score") + 14);
        String sc2 = t2.substring(0, t2.indexOf("}"));
        Serial.println("[" + sc1 + "|" + sc2 + "]");
        set_score1(sc1.toInt());
        set_score2(sc2.toInt());
      } else if (s.indexOf("team_1_score") > -1) {
        String sc1 = s.substring(s.indexOf("team_1_score") + 14, s.indexOf("}"));
        Serial.println("[" + sc1 + "|sc1]");
        set_score1(sc1.toInt());
              
      } else if (s.indexOf("team_2_score") > -1) {
        String sc2 = s.substring(s.indexOf("team_2_score") + 14, s.indexOf("}"));
        Serial.println("[" + sc2 + "|sc2]");
        set_score2(sc2.toInt());
        
      }
    }
  }
  if (game_started && !score_proc.running()) {
    game_started = 0;
  }
}

void listen_table() {
  board_id = get_yun_mac();
  Serial.println("listening on:" + board_id);
  //if (game_proc.running()) {
  //  game_proc.close();
  //}
  game_proc.runShellCommandAsynchronously("curl --no-buffer -L -k -H 'Accept: text/event-stream' '" + DATA_NAME + "/tables/" + board_id + "/current_game.json" + DATA_AUTH + "'");
}

void listen_game(String game_id) {
  if (score_proc.running()) {
    score_proc.close();
  }
  score_proc.runShellCommandAsynchronously("curl --no-buffer -L -k -H 'Accept: text/event-stream' '" + DATA_NAME + game_id + ".json" + DATA_AUTH + "'");
}

String get_yun_mac() {
  Process p;
  p.runShellCommand("ifconfig eth1 | grep HWaddr | awk '{print $5}' | sed -e 's/://g'");
  String s;
  while (p.available()) {
    s = p.readString();
    s.trim();
  }
  Serial.println("YUN MAC is: " + s + "|");
  return s;
}

void set_score1(int score) {
  Serial.println("1-" + String(score));
  if (score == score1) {
    return;
  }
  for (int i = 0; i < NUM_PIXELS; i++) {
    if (i < (score % 10)) {
      pixel_strip1.setPixelColor(i, 0x0000FF);
    } else {
      pixel_strip1.setPixelColor(i, 0x000000);
    }
  }
  score2 = score;
  pixel_strip1.show();
}

void set_score2(int score) {
  Serial.println("2-" + String(score));
  if (score == score2) {
    return;
  }
  for (int i = 0; i < NUM_PIXELS; i++) {
    if (i < (score % 10)) {
      pixel_strip2.setPixelColor(i, 0xFF0000);
    } else {
      pixel_strip2.setPixelColor(i, 0x000000);
    }
  }
  score2 = score;
  pixel_strip2.show();
}

void animate_strips(uint32_t color) {
  for (int i = 0; i < NUM_PIXELS; i++) {
    delay(20);
    pixel_strip1.setPixelColor(i, color);
    pixel_strip2.setPixelColor(i, color);
    pixel_strip1.show();
    pixel_strip2.show();      
  }
  for (int i = 0; i < NUM_PIXELS; i++) {
    delay(20);
    pixel_strip1.setPixelColor(NUM_PIXELS - i - 1, 0);     
    pixel_strip2.setPixelColor(NUM_PIXELS - i - 1, 0);      
    pixel_strip1.show();
    pixel_strip2.show(); 
  } 
}

void send_button_click(int button_index) {
  String url = DATA_NAME + "/switches/" + button_names[button_index] + "-"+ board_id + "/hits.json";
  Serial.println(url);
  HttpClient client;
  client.noCheckSSL();
  client.post(url, DATA_PAYLOAD);
  while (client.available()) {
    char c = client.read();
    Serial.print(c);
  }
}

void update_display() {
  u8g.setFont(u8g_font_profont10);
  u8g.setPrintPos(15, 15);
  u8g.print(MSG_BANNER);
  //u8g.setFont(u8g_font_profont10);
  u8g.setPrintPos(0, 35);
  u8g.print(status_message);
  u8g.setPrintPos(64, 35);
  u8g.print(ip_message);  
  //u8g.setFont(u8g_font_profont22);
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (button_values[i] == LOW) {
      u8g.setPrintPos(i * 32, 60);
      u8g.print(button_names[i]);   
    }
  }
}

// Here endeth the lesson.
