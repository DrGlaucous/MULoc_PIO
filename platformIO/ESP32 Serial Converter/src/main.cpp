#include <Arduino.h>

//115200
#define BAUD_RATE 460800

void setup()
{
	//initial setup
	Serial.begin(BAUD_RATE);
	Serial2.begin(BAUD_RATE, SERIAL_8N1, 16, 17);

	//set the onboard LED for debug output
	pinMode(22, OUTPUT);
	digitalWrite(22, true);

}
void loop()
{
	//send all data from one serial port to the other and vice-versa
	while(Serial2.available()) {
		digitalWrite(22, false);
		auto in_byte = Serial2.read();
		Serial.write(in_byte);
	}
	while(Serial.available()) {
		digitalWrite(22, false);
		auto in_byte = Serial.read();
		Serial2.write(in_byte);
	}

	digitalWrite(22, true);

}
