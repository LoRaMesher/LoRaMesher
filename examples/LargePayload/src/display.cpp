#include "display.h"

Display::Display() {
}

void Display::drawDisplay() {
    display.clearDisplay();

    printLine(displayText[0], x1, 0, 1, minX1, move1);
    printLine(displayText[1], x2, 9, 2, minX2, move2);
    printLine(displayText[2], x3, 27, 2, minX3, move3);

    if (routingSize > 1) {
        String line4Text;
        String line5Text;

        for (int i = 0; i < routingSize / 2; i++) {
            line4Text += routingText[i];
            line5Text += routingText[i + routingSize / 2];
        }

        if (routingSize % 2 > 1)
            line4Text += routingText[routingSize - 1];

        line4Text += "|";
        line5Text += "|";

        printLine(line4Text, x4, 45, 1, minX4, move4);
        printLine(line5Text, x5, 54, 1, minX5, move5);

    } else
        printLine(routingText[0] + "|", x4, 45, 1, minX4, move4);

    display.display();

    vTaskDelay(10 / portTICK_PERIOD_MS);
}

void Display::printLine(String str, int& x, int y, int size, int minX, bool move) {
    display.setTextSize(size);

    display.setCursor(x, y);
    display.print(str);

    if (move) {
        x = x - 2;
        if (x < minX) x = display.width();
    }
}

void Display::changeLineOne(String text) {
    changeLine(text, 0, x1, minX1, 1, move1);
}

void Display::changeLineTwo(String text) {
    changeLine(text, 1, x2, minX2, 2, move2);
}

void Display::changeLineThree(String text) {
    changeLine(text, 2, x3, minX3, 2, move3);
}

void Display::changeRoutingText(String text, int position) {
    routingText[position] = text;
}

void Display::changeSizeRouting(int size) {
    routingSize = size;
}

void Display::changeLineFour() {
    if (routingSize / 2 * routingText[0].length() > 20) {
        x4 = x5 = display.width();
        int minX = -(6) * routingText[0].length();
        minX5 = minX * routingSize / 2;
        minX4 = minX * (routingSize / 2 + routingSize % 2);
        move4 = move5 = true;
    } else {
        x4 = x5 = 0;
        move4 = move5 = false;
    }
}

void Display::changeLine(String text, int pos, int& x, int& minX, int size, bool& move) {
    if (text.length() > 10) {
        x = display.width();
        minX = -(6 * size) * text.length();
        move = true;
    } else {
        x = 0;
        move = false;
    }

    displayText[pos] = text;
}


void Display::initDisplay() {
    Serial.println(F("SSD1306 allocation Done"));

    // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;); // Don't proceed, loop forever
    }

    display.clearDisplay();

    display.setTextColor(WHITE); // Draw white text

    display.setTextWrap(false);

    delay(50);
}

Display Screen = Display();
