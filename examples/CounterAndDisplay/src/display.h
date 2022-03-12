#pragma once

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)

class Display {
public:
    Display();
    void initDisplay();
    void changeLineOne(String str);
    void changeLineTwo(String str);
    void changeLineThree(String str);
    void changeLineFour();
    void drawDisplay();
    void changeRoutingText(String text, int position);
    void changeSizeRouting(int size);

private:
    Adafruit_SSD1306 display = Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT);
    TaskHandle_t Display_TaskHandle = NULL;

    void changeLine(String str, int pos, int& x, int& minX, int size, bool& move);
    void printLine(String str, int& x, int y, int size, int minX, bool move);

    String displayText[3] = {"LoRa Mesher", "LoRa Mesher", "LoRa Mesher"};

    String routingText[25];
    int routingSize = 0;

    bool move1, move2, move3, move4, move5 = true;
    int x1, minX1, x2, minX2, x3, minX3, x4, minX4, x5, minX5;
};

extern Display Screen;