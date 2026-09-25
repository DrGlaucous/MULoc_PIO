#pragma once



//the pins on the NRF chip are scrambled because the arduino backend expects this chip to be on a different devboard,
//I'll put the fixed mapping(s) here
//turns out you don't need this if the board variant is set to generic.
//onboard LEDs for the DWM3001CDK
// #define P0_14 3
// #define P0_22 10
// #define P0_05 20
// #define P0_04 15

#define LED_D9 4
#define LED_D10 5
#define LED_D11 22
#define LED_D12 14

//onboard pushbuttons for the DWM3001CDK
#define SWITCH_1 18
#define SWITCH_2 2

//adding 32 gives us pin bank 2.
#define SPI_CS 32 + 6
#define SPI_CLK 3
#define SPI_MOSI 8
#define SPI_MISO 29

//other radio control pins
#define DW_RST 25
#define DW_IRQ 32 + 2
#define DW_WUP 32 + 19


// // Arduino board mappings
// #define ARDUINO_13_PIN              NRF_GPIO_PIN_MAP(0,  3)  // used as DW3000_CLK_Pin
// #define ARDUINO_12_PIN              NRF_GPIO_PIN_MAP(0, 29)  // used as DW3000_MISO_Pin
// #define ARDUINO_11_PIN              NRF_GPIO_PIN_MAP(0,  8)  // used as DW3000_MOSI_Pin
// #define ARDUINO_10_PIN              NRF_GPIO_PIN_MAP(1,  6)  // used as DW3000_CS_Pin ->38
// #define ARDUINO_9_PIN               NRF_GPIO_PIN_MAP(1, 19)  // used as DW3000_WKUP_Pin
// #define ARDUINO_8_PIN               NRF_GPIO_PIN_MAP(1,  2)  // used as DW3000_IRQ_Pin
// #define ARDUINO_7_PIN               NRF_GPIO_PIN_MAP(0, 25)  // used as DW3000_RST_Pin

// #define DW3000_RST_Pin      ARDUINO_7_PIN //reset pin
// #define DW3000_IRQ_Pin      ARDUINO_8_PIN //interrupt pin
// #define DW3000_WUP_Pin      ARDUINO_9_PIN //wakeup pin

// // SPI defs
// #define DW3000_CS_Pin       ARDUINO_10_PIN
// #define DW3000_CLK_Pin      ARDUINO_13_PIN  // DWM3000 shield SPIM1 sck connected to DW1000 ->3
// #define DW3000_MOSI_Pin     ARDUINO_11_PIN  // DWM3000 shield SPIM1 mosi connected to DW1000 ->8
// #define DW3000_MISO_Pin     ARDUINO_12_PIN  // DWM3000 shield SPIM1 miso connected to DW1000 ->29







