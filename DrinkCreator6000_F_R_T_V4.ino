#include <Arduino_FreeRTOS.h>
#include <semphr.h>
#include <OneWire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <EEPROM.h>
#include <Wire.h>
#include "FreeShiftOut.h"
#include "LcdCharacters.h"
#include "CustomDataTypes.h"

enum {DSPin=PIN_PC0, STPin=PIN_PC1, SHPin=PIN_PC2,OEnable=PIN_PC3,INTPin=PIN_PD2,THERMOMETER_PIN=PIN_PD3,Pelt1Pin=PIN_PD4,Pelt2Pin=PIN_PD5};
enum {EEPROM_BOOT_UPS_ADDRESS=0};
enum {temperatureHistAddress=1};
enum {temperatureSetAddress=2};
enum {pumpEf=200}; // mL/min
enum {liczbaDrinkow=3, liczbaSkladnikow=8};
enum {E_WELCOME=0,E_SELECTION=1,EKRAN_SHOW_INFO=2,E_TEMP_INFO=3,E_ORDER_DRINK=4,E_TEST_PUMPS=5};
enum {LOADING_BAR=17};
enum {GREEN_BUTTON=1,L_WHITE_BUTTON=2,R_WHITE_BUTTON=4,BLUE_BUTTON=8,RED_BUTTON=16};
//Adres ekspandera 0x20
//Adres LCD 0x27
OneWire thermometer(THERMOMETER_PIN);
LiquidCrystal_I2C lcd(0x27, 20, 4);

const char ingredientsName[liczbaSkladnikow][13]={"Whiskey","Jager","Prosecco","Aperol","Cola","Sprite","Woda gaz.","Sok"};
volatile static float currentDrinkTemperature=0;
volatile static byte setDrinkTemperature=10;
volatile static byte setDrinkTemperatureHist=2;
volatile static byte screenId;
volatile static byte drinkId;
volatile static byte pumpTestId;
static int bootUpsAmount;

TaskHandle_t my_setup_handle;
TaskHandle_t h_activatePumpId;
TaskHandle_t h_showInfo;

SemaphoreHandle_t sem_ReadData;
SemaphoreHandle_t semI2C_LOCK;
SemaphoreHandle_t sem_OrderStart;
SemaphoreHandle_t sem_OrderStop;

QueueHandle_t qScreenData;
QueueHandle_t qDrinkData;

sDrinkData drink[3]={{"Raz 1", 50, 0, 0, 0, 0, 0, 0, 0, 0},
  {"Dwa 2", 200, 200, 0, 0, 0, 0, 0, 0, 0},
  {"Trzy 3", 200, 0, 200, 0, 0, 0, 0, 0, 0}
};

const sDrinkData emptyDrink={};
void updateBootUps(){
	bootUpsAmount=EEPROM.read(EEPROM_BOOT_UPS_ADDRESS);
	bootUpsAmount++;
	if(bootUpsAmount==0)
		bootUpsAmount=1;
	
	EEPROM.write(EEPROM_BOOT_UPS_ADDRESS,bootUpsAmount);
}
void taskUpdateScreen(void*pvParameters){
  //Nie ma potrzeby tego juz chyba ruszac
	sScreenData receivedLcdData;
	for(;;){
		if(xQueueReceive(qScreenData,&receivedLcdData,portMAX_DELAY)==pdPASS){
			if(xSemaphoreTake(semI2C_LOCK,portMAX_DELAY)){
				for(byte i=0;i<4;i++){
					lcd.setCursor(0,i);
					for(byte j=0;j<20;j++)
					if(receivedLcdData.lines[i][j]==0)
						lcd.write(' ');
					else if(receivedLcdData.lines[i][j]==LOADING_BAR)
						lcd.write(0);
					else if(receivedLcdData.lines[i][j]==18)
						lcd.write(1);
					else if(receivedLcdData.lines[i][j]==19)
						lcd.write(2);
					else if(receivedLcdData.lines[i][j]==20)
						lcd.write(3);
					else if(receivedLcdData.lines[i][j]==21)
						lcd.write(4);
					else if(receivedLcdData.lines[i][j]==22)
						lcd.write(5);
					else if(receivedLcdData.lines[i][j]==23)
						lcd.write(6);
					else if(receivedLcdData.lines[i][j]==24)
						lcd.write(7);
					else
						lcd.write(receivedLcdData.lines[i][j]);
				}
				lcd.setCursor(receivedLcdData.lcdCursorX,receivedLcdData.lcdCursorY);
				if(receivedLcdData.lcdCursorBlink)
					lcd.blink();
				else
					lcd.noBlink();
				xSemaphoreGive(semI2C_LOCK);
			}
		}
	}
}
void taskWelcomeScreen(){
  //Nie ma potrzeby tego juz chyba ruszac
	sScreenData lcdData;
	byte counter=0;
    for(;;){
      counter++;
		for(byte k=0;k<4;k++){
			byte buffer[20]={0};
			if(k==0){
				memcpy(lcdData.lines[k],buffer,20);
				strncpy(lcdData.lines[k],"Drink Creator 6000",20);
				continue;
			}
			else if(k==1){
				memcpy(lcdData.lines[k],buffer,20);
				strncpy(lcdData.lines[k],"Wersja 2.0",20);
				continue;
			}
			else if(k==2){
				memcpy(lcdData.lines[k],buffer,20);
				sprintf(lcdData.lines[k],"Uruchomienie nr: %d",bootUpsAmount,20);
				continue;
			}
			else if(k==3) {
				memcpy(lcdData.lines[k],buffer,20);
				sprintf(lcdData.lines[k],"Test %d",counter,20);
				continue;
			}
		}
    if(counter<4){
      xQueueSend(qScreenData,&lcdData,0);
    }
    if(counter==4){
          screenId=E_SELECTION;
          updateDrink(0);
          vTaskDelete(NULL);
        }
      vTaskDelay(1000/portTICK_PERIOD_MS);
    }
}
void stopPumps(){
  //sDrinkData sendDrinkData={0};
  xQueueSend(qDrinkData,&emptyDrink,0);
  //vTaskResume(h_activatePumpId);
  //xTaskNotify(h_activatePumpId,2,eSetValueWithOverwrite);
}
void processKeyboard(byte keyboardInput){
	bool f_updateDrink=false;
	bool f_showInfo=false;
	bool f_showTempInfo=false;
	bool f_orderDrink=false;
	bool f_stopPumps=false;
	bool f_testPumps=false;
	bool f_activatePumpId=false;

	switch(screenId){
		case E_SELECTION:{
			if((keyboardInput&R_WHITE_BUTTON)==0){
				drinkId++;
				if(drinkId>liczbaDrinkow)
					drinkId=liczbaDrinkow;
				f_updateDrink=true;
			}
			else if((keyboardInput&L_WHITE_BUTTON)==0){
				drinkId--;
				if(drinkId<1)
					drinkId=1;
				f_updateDrink=true;
			}
			else if((keyboardInput&GREEN_BUTTON)==0){
				f_orderDrink=true;
			}
			break;
		}
   case E_ORDER_DRINK:{
    if((keyboardInput&RED_BUTTON)==0){
      f_stopPumps=true;
    }
    break;
   }
	}
	if(f_updateDrink){
		screenId=E_SELECTION;
		updateDrink(1);
	}
	else if(f_showInfo){
		screenId=EKRAN_SHOW_INFO;
		//taskShowInfo();
	}
	else if(f_showTempInfo){
		screenId=E_TEMP_INFO;
	}
	else if(f_orderDrink){
		screenId=E_ORDER_DRINK;
       xQueueSend(qDrinkData,&drink[drinkId-1],0);
       //vTaskPrioritySet(h_activatePumpId,2);
   xTaskCreate(taskActivatePumps,"Zamawianie",400,NULL,1,&h_activatePumpId);
    //xTaskNotify(h_activatePumpId,1,eSetValueWithOverwrite);
	}
  else if(f_stopPumps){
    screenId=E_SELECTION;
    stopPumps();
  }
}
void readInput(void*pvParameters){
	byte keyboardInput=0;
	for(;;){
		if(xSemaphoreTake(sem_ReadData,portMAX_DELAY)==pdTRUE){
			if(xSemaphoreTake(semI2C_LOCK,portMAX_DELAY)==pdTRUE){
				Wire.requestFrom(0x20,1);
				while(Wire.available())
					keyboardInput=Wire.read();
				Wire.endTransmission();
				processKeyboard(keyboardInput);
				xSemaphoreGive(semI2C_LOCK);
			}
			else
				//xSemaphoreGive(semI2C_LOCK);
        1;
		}
   //Tu bedzie sprawdzanie czy przyszlo jakies powiadomienie
   //vTaskDelay(100/portTICK_PERIOD_MS);
	}
}
void setInputFlag(){
	xSemaphoreGiveFromISR(sem_ReadData,NULL);
}
void updateDrink(int awaitTime){
  sScreenData test={"", "", "", ""};
  byte length=0;
  for(byte k=0;k<4;k++){
    byte buffer[20]={0};
    if(k==0){
      memcpy(test.lines[k],buffer,20);
      strncpy(test.lines[k],drink[drinkId-1].drinkName,20);
      continue;
    }
    length=strlen(ingredientsName[k-1]);
    memcpy(buffer,ingredientsName[k-1],length);
    length+=sprintf(buffer+length+1,"%d",drink[drinkId-1].amountOfIngredient[k-1]);
    memcpy(test.lines[k],buffer, 20);
    memcpy(test.lines[k]+length+2,"ml",2);
  }
  xQueueSend(qScreenData,&test,awaitTime/portTICK_PERIOD_MS);
  //To jest tymczasowo potem dodam tam scrollowanie
}
void regulateTemp(void*pvParameters){
  for(;;){
    //digitalWrite(Pelt1Pin,digitalRead(Pelt1Pin)^1);
    //digitalWrite(Pelt2Pin,digitalRead(Pelt2Pin)^1);
    digitalWrite(Pelt1Pin,HIGH);
    digitalWrite(Pelt2Pin,HIGH);
    
    vTaskDelay(5000/portTICK_PERIOD_MS);
  }
}
void taskActivatePumps(void*pvParameters){
	sDrinkData d={};
	byte bitOrder=LSBFIRST;
	int dur[8]={0};
	byte activatePumpMask=0;
	byte i=0;
	int maxVolume=0;
	int counter=0;
	bool isActive=false;
	int32_t notification=0;
	for(;;){
			if(xQueueReceive(qDrinkData,&d,300/portTICK_PERIOD_MS)==pdPASS){
				sScreenData lcdData={0};
				for(byte i=0;i<8;i++){
					dur[i]=d.amountOfIngredient[i]*60000/300/200;
				}
				maxVolume=0;
				for(byte i=0;i<8;i++)
					maxVolume+=dur[i];
				counter=0;
               isActive=true;
				if(maxVolume==0){
					digitalWrite(STPin,LOW);
					freeShiftOut(DSPin,SHPin,0,LSBFIRST);
					digitalWrite(STPin,HIGH);
					vTaskDelete(NULL);
					//Wyslanie powiadomienia ze anulowano
          //vTaskSuspend(NULL);
				}
				maxVolume=dur[0];
				for(byte i=1;i<8;i++){
					if(dur[i]>maxVolume)
						maxVolume=dur[i];
				}
			}
			else{
              digitalWrite(STPin,LOW);
        freeShiftOut(DSPin,SHPin,activatePumpMask,LSBFIRST);
        digitalWrite(STPin,HIGH);
				activatePumpMask=0;
				for(i=0;i<8;i++){
					if(i==0)
						activatePumpMask|=(dur[i]>0);
					else
						activatePumpMask|=((dur[i]>0)<<i);
				}
				digitalWrite(STPin,LOW);
				freeShiftOut(DSPin,SHPin,activatePumpMask,LSBFIRST);
				digitalWrite(STPin,HIGH);
        int remaining=0;
        for(i=0;i<8;i++)
          remaining+=dur[i];
        if(remaining==0){
          //Powiadomienie ze ukonczono //i powiadomienie do samego siebie nawet
          isActive=false;
          xQueueSend(qDrinkData,&emptyDrink,0);
        }
				for(i=0;i<8;i++){
					dur[i]-=1;
					if(dur[i]<0)
						dur[i]=0;
				}
				counter++;
				sScreenData lcdData{0};
				for(byte k=0;k<4;k++){
					byte buffer[20]={0};
					if(k==0){
						memcpy(lcdData.lines[k],buffer,20);
						strncpy(lcdData.lines[k],"Zamawianie drinka",20);
						continue;
					}
					else if(k==1){
						memcpy(lcdData.lines[k],buffer,20);
						strncpy(lcdData.lines[k],drink[drinkId-1].drinkName,20);
					}
					else if(k==2){
						memcpy(lcdData.lines[k], buffer, 20);
					}
					else if(k==3){
						memcpy(lcdData.lines[k],buffer,20);
						for(byte j=0;j<20;j++){
							if(counter<=maxVolume*j/20)
								lcdData.lines[k][j]=0;
							else
								lcdData.lines[k][j]=LOADING_BAR;
						}
					}
				}
				xQueueSend(qScreenData,&lcdData,0);
		}
	}
}
void taskTestowy(void*pvParameters){
  sDrinkData d{0};
  //sScreenData s{0};
  byte state=255;
  bool active=0; //state 0 blocked state 1 running
  uint32_t werdon;
  uint32_t notification;
  for(;;){
    xTaskNotifyWait(0,0,&notification,0);
    if(notification==1){
      active=true;
    }
    if(notification==2)
      active=false;
    
    if(active){
        digitalWrite(STPin, LOW);
        freeShiftOut(DSPin,SHPin,state,LSBFIRST);
        digitalWrite(STPin,HIGH);
    }
    else{
      digitalWrite(STPin,LOW);
      freeShiftOut(DSPin,SHPin,0,LSBFIRST);
      digitalWrite(STPin,HIGH);
    }
    vTaskDelay(100/portTICK_PERIOD_MS);
  }
}
void systemStartUp(void*pvParameters){
	pinMode(STPin, OUTPUT);
	digitalWrite(STPin, HIGH);
	pinMode(DSPin, OUTPUT);
	digitalWrite(DSPin, HIGH);
	pinMode(SHPin, OUTPUT);
	digitalWrite(SHPin, HIGH);
	pinMode(OEnable, OUTPUT);
	digitalWrite(OEnable, LOW);

	digitalWrite(STPin,LOW);
	freeShiftOut(DSPin,SHPin,0,LSBFIRST);
	digitalWrite(STPin,HIGH);
//Inicjalizacja oraz zerowanie 74HC595
	pinMode(INTPin, INPUT_PULLUP);
	sem_ReadData=xSemaphoreCreateBinary();
	if(sem_ReadData!=NULL)
		attachInterrupt(digitalPinToInterrupt(INTPin),setInputFlag,FALLING);
	sem_ReadData=xSemaphoreCreateBinary();
//Inicjalizacja mutexu obslugujacego przerwanie klawiatury
	semI2C_LOCK=xSemaphoreCreateMutex();
	xSemaphoreGive(semI2C_LOCK);
//Inicjalizacja mutexu blokujacego dostep do magistrali I2C
	pinMode(Pelt1Pin, OUTPUT);
	digitalWrite(Pelt1Pin,LOW);
	pinMode(Pelt2Pin, OUTPUT);
	digitalWrite(Pelt2Pin,LOW);
//Inicjalizacja oraz zerowanie peltierow
	drinkId=1;
	//screenId=E_WELCOME;
 screenId=1;
	pumpTestId=1;
//Inicjalizacja zmiennych globalnych	
	qScreenData=xQueueCreate(1,sizeof(sScreenData));
	qDrinkData=xQueueCreate(1,sizeof(sDrinkData));
//Utworzenie kolejek obslugujacych przeplyw informacji w systemie
	Wire.begin();
	lcd.begin();
	lcd.backlight();
	lcd.clear();
	lcd.createChar(0,fullSquare);
	lcd.createChar(1,fullSquare);
	lcd.createChar(2,fullSquare);
	lcd.createChar(3,fullSquare);
	lcd.createChar(4,fullSquare);
	lcd.createChar(5,fullSquare);
	lcd.createChar(6,fullSquare);
	lcd.createChar(7,fullSquare);
//Inicjalizacja magistrali I2C oraz wyswietlacza LCD
	updateBootUps();
//Aktualizacja liczby uruchomien systemu
	xTaskCreate(readInput,"Wejscia",350,NULL,1,NULL);
	xTaskCreate(taskUpdateScreen,"Ekran",250,NULL,1,NULL);
	xTaskCreate(regulateTemp,"Termostat",200,NULL,1,NULL);
  xTaskCreate(taskWelcomeScreen,"Powitanie",300,NULL,1,NULL); //To jako ostatnie musi byc dodawane
	//xTaskCreate(taskActivatePumps,"Zamawianie",400,NULL,1,&h_activatePumpId);
  //xTaskCreate(readDrinkTemperature,"Termometr",400,NULL,1,NULL);
	//xTaskCreate(taskTestowy,"Test",300,NULL,1,&h_activatePumpId);
 //vTaskSuspend(h_activatePumpId);
//Utworzenie procesow w systemie
	vTaskDelete(my_setup_handle);
//Usuniecie procesu inicjalizujacego
}
void setup(){
  xTaskCreate(systemStartUp,"Startup",200,NULL,3,&my_setup_handle);
}
void loop(){

}
