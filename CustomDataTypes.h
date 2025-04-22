#ifndef CUSTOM_DATA_TYPES_H
#define CUSTOM_DATA_TYPES_H

struct sDrinkData {
  char drinkName[20];
  int amountOfIngredient[8];
  byte numberOfOrders;
};
//struct to pass to taskActivatePumps(void*)
struct sScreenData {
  char lines[4][20];
  byte lcdCursorX;
  byte lcdCursorY;
  bool lcdCursorBlink;
};
//struct to pass to taskUpdateScreen(void*)
#endif
