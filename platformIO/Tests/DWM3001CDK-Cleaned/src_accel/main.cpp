#include <vector>
#include <Arduino.h>
#include "constants.h"
#include "dw3000.h"
#include "dw3000_regs.h"
#include "dw3000_shared_defines.h"
//#include "dw3000_device_api.h"
#include "SPI.h"
#include "Wire.h"

//add to platformIO ini:
//https://github.com/sparkfun/SparkFun_LIS2DH12_Arduino_Library.git
#include "SparkFun_LIS2DH12.h"

SPARKFUN_LIS2DH12 accelerometer;


//starts SPI, Serial, and the DW3000
void setup() {


	// sets up the device to use the pins on the bottom left of the rPi header for serial communication.
	Serial = Uart(NRF_UART0, UARTE0_UART0_IRQn, 31, 7);
	Serial.begin(BAUD_RATE);
	Serial.println("Begin");

    //start i2c on the proper pins
    Wire = TwoWire(NRF_TWIM1, NRF_TWIS1, SPIM1_SPIS1_TWIM1_TWIS1_SPI1_TWI1_IRQn, I2C0_SDA, I2C0_SCL);
    Wire.begin();

    if(!accelerometer.begin())
    {
        while(1)
        {
            Serial.println("Could not find accelerometer");
            delay(1000);
        }
    }

    Serial.println("Ready");
}

float lastx = 0.0;
float lasty = 0.0;
float lastz = 0.0;

void loop() {

    //TwoWire Wire(NRF_TWIM1, NRF_TWIS1, SPIM1_SPIS1_TWIM1_TWIS1_SPI1_TWI1_IRQn, PIN_WIRE_SDA, PIN_WIRE_SCL);

    if(accelerometer.available())
    {
        float x = accelerometer.getX();
        float y = accelerometer.getY();
        float z = accelerometer.getZ();

        float jerkx = x - lastx;
        float jerky = y - lasty;
        float jerkz = z - lastz;

        lastx = x;
        lasty = y;
        lastz = z;

        Serial.print(jerkx);
        Serial.print(",");
        Serial.print(jerky);
        Serial.print(",");
        Serial.print(jerkz);
        Serial.println("");


    }
    delay(30);


}















