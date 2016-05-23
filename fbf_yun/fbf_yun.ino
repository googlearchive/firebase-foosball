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
#include <HttpClient.h>
#include "U8glib.h"
#include <elapsedMillis.h>
#include <Mailbox.h>

#define       YELLOW                0xAAFF00;
#define       PIN_DATA1             4
#define       PIN_CLK1              5
#define       PIN_DATA2             6
#define       PIN_CLK2              7
#define       NUM_BUTTONS           4
#define       NUM_PIXELS            10
#define       INTERVAL_DISPLAY      1000

String        MSG_BANNER          = "FIREBASE  FOOSBALL";
int           button_pins[]       = {A0, A1, A2, A3};
const char*   button_names[]      = {"B0", "B1", "B2", "B3"};
int           button_values[]     = {HIGH, HIGH, HIGH, HIGH};
elapsedMillis animation_timer;
elapsedMillis display_timer;

String        board_id = "unset";
String        eth_ip = "unset";

U8GLIB_SH1106_128X64 u8g(U8G_I2C_OPT_DEV_0|U8G_I2C_OPT_FAST);
Adafruit_DotStar pixel_strip1 = Adafruit_DotStar(NUM_PIXELS, PIN_DATA1, PIN_CLK1, DOTSTAR_BGR);
Adafruit_DotStar pixel_strip2 = Adafruit_DotStar(NUM_PIXELS, PIN_DATA2, PIN_CLK2, DOTSTAR_BGR);

Adafruit_DotStar* pixel_strips[] = {&pixel_strip1, &pixel_strip2};

int scores[2] = {0, 0}; 
uint32_t colors[2] = {0x0000FF, 0xFF0000};

#define DEBUG 0

void setup() {
  pixel_strip1.begin();
  pixel_strip2.begin();
  pixel_strip1.show();
  pixel_strip2.show();
  animate_strips(0xFFFFFF);
  if (DEBUG) {
    Serial.begin(115200);
    while (!Serial) {}
    Serial.println("setup >");
    Serial.println("bridge >");
  }
  Bridge.begin();
  if (DEBUG) Serial.println("bridge <");
  Mailbox.begin();
  for (int i = 0; i < NUM_BUTTONS; i++) {
    pinMode(button_pins[i], INPUT_PULLUP);
  }
  animate_strips(0xFFFFFF);
  animate_strips(0xFFFFFF);
  update_display();
  if (DEBUG) Serial.println("setup <");

}

void loop() {
  iter_buttons();
  iter_mailbox();
  iter_timers();
}

void iter_mailbox() {
  if (Mailbox.messageAvailable()) {
    // read all the messages present in the queue
    while (Mailbox.messageAvailable()) {
      String msg;
      Mailbox.readMessage(msg);
      String cmd = msg.substring(0, 3);
      String val = msg.substring(4);
      if (DEBUG) Serial.println("msg> cmd>" + cmd + ">val>" + val + ">");
      if (cmd.equals("sco")) {
        int space = val.indexOf(" ");
        String s1 = val.substring(0, space);
        String s2 = val.substring(space + 1);
        set_score(s1.toInt(), s2.toInt());
        display_score();
        if (DEBUG) Serial.println("score <" + s1 + "|" + s2 + ">");
      }
      if (cmd.equals("mac")) {
        if (DEBUG) Serial.println("mac is>" + val);
        board_id = val;
      }
       if (cmd.equals("eip")) {
        if (DEBUG) Serial.println("eip is>" + val);
        eth_ip = val;
      }
    }
  }
}


void iter_buttons() {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    int val = digitalRead(button_pins[i]);
    if (val != button_values[i]) {
      button_values[i] = val;
      if (val == LOW) {
        uint32_t color;
        send_button_click(i);
        if (i == 0) {
          set_score(scores[0] + 1, -1);
          color = 0x0000FF;
        }
        if (i == 1) {
          set_score(-1, scores[1] + 1);
          color = 0xFF0000;
        }
        if (i == 2) {
          color = YELLOW;
        }
        if (i == 3) {
          color = 0x00FF00;
        }
        animate_strips(color, 3);
        display_score();
        
      }
    }
  }
}


void iter_timers() {
  if (display_timer > 1000) {
    display_timer = 0;
    update_display();
  } 
}


int set_score(int s1, int s2) {
  if (s1 > -1) {
    scores[0] = s1;
  }
  if (s2 > -1) {
    scores[1] = s2;
  }
}

void display_score() {
  for (int i = 0; i < 2; i++) {
    int score = scores[i];
    int mscore = score % 11;
    if (score > 10) {
      mscore = mscore + 1;
    }
    for (int j = 0; j < NUM_PIXELS; j++) {
      uint32_t color;
      if (j < mscore) {
        color = colors[i];
      } else {
        color = 0x000000;
      }
      pixel_strips[i]->setPixelColor(j, color);
    }
  }
  for (int i = 0; i < 2; i++) {
    pixel_strips[i]->show();
  }
}

void animate_strips(uint32_t color) {
  animate_strips(color, 1);
}

void animate_strips(uint32_t color, int count) {
  for (int c = 0; c < count; c++) {
    for (int i = 0; i < NUM_PIXELS; i++) {
      delay(10);
      pixel_strip1.setPixelColor(i, color);
      pixel_strip2.setPixelColor(i, color);
      pixel_strip1.show();
      pixel_strip2.show();      
    }
    for (int i = 0; i < NUM_PIXELS; i++) {
      delay(10);
      pixel_strip1.setPixelColor(NUM_PIXELS - i - 1, 0);     
      pixel_strip2.setPixelColor(NUM_PIXELS - i - 1, 0);      
      pixel_strip1.show();
      pixel_strip2.show(); 
    }
  }  
}

void send_button_click(int button_index) {
  if (DEBUG) Serial.println(button_index);
  Mailbox.writeMessage(button_names[button_index]);
}

void update_display() {
  u8g.firstPage();  
  do {
    u8g.setFont(u8g_font_profont15);
    u8g.setPrintPos(0, 10);
    u8g.print(MSG_BANNER);
    u8g.setFont(u8g_font_profont10);
    u8g.setPrintPos(0, 26);
    u8g.print("bdid> " + board_id);
    u8g.setPrintPos(0, 36);
    u8g.print("eth1> " + eth_ip);
    u8g.setPrintPos(0, 46);  
    u8g.print("w");
    u8g.setFont(u8g_font_profont15);
    u8g.setPrintPos(0, 63);  
    u8g.print(String(scores[0]) + " " + String(scores[1]));
        u8g.setFont(u8g_font_profont10);

     u8g.setPrintPos(48, 62);  
    u8g.print("semper legitimi");
    
  } while( u8g.nextPage() );
}
  
// Here endeth the lesson.
