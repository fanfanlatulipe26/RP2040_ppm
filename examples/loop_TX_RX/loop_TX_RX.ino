/*
   Test in loopback mode.
   On the same RP2040 we create a channel for writing a PPM stream on pin tx_pin
   and a channel for reading on pin rx_pin. There is a jumper between both pins.
   Please, update de #define for the pins, following your configuration.
   The writer is started in testmode and generate PPM frames where all channels have the
   same values. This value is incremented for each frame and varies between 1000 and 1999.
*/   
#include "RP2040_ppm.h"

#define rx_pin 4  // put a jumper between pin 4 and 3
#define tx_pin 3

ppmChannels_t ppmArray;
ppmChannels_t ppmArrayTX;

ppmReader myPPM_RX;
ppmWriter myPPM_TX;
#define incr 25

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.printf("Starting loopback test example. Put jumper between pin %d et pin %d\n", rx_pin, tx_pin);

  ppmArray = myPPM_RX.begin(rx_pin);
  ppmArrayTX = myPPM_TX.begin(tx_pin);  // create a 8 channels PPM writer, RISING style
  myPPM_TX.startTest(incr);
  myPPM_RX.start();
  // just for information, we print the first 10 frames received, with the timestamp
  delay(100);
  int cnt = 0;
  do {
    if (myPPM_RX.newFrame())
    {
      cnt++;
      for (uint i = 1; i <= ppmArray[0]; i++) Serial.printf(" % d  ", ppmArray[i]);
      Serial.println();
    }
  } while (cnt < 20);
  Serial.println("Done !!");
}

void loop() {
}
