#ifndef FREE_SHIFT_OUT_H
#define FREE_SHIFT_OUT_H

void freeShiftOut(byte dataPin,byte clockPin,byte data,byte bitOrder){
	for(byte i=0;i<8;i++){
        if(bitOrder==MSBFIRST)
          digitalWrite(dataPin,!!(data&(1<<i)));
        else
          digitalWrite(dataPin,!!(data&(1<<(7-i))));
        digitalWrite(clockPin,LOW);
		  digitalWrite(clockPin,HIGH);
    }	
}

#endif
