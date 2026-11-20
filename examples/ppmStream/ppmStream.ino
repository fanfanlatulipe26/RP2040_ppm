/*
   This example create a 8 channels PPM stream with 
    default timming & size values.
   Channels values are endless changed.
   ( A small USB logic analyzer is handy for the control of PPM stream.)
*/
#include <RP2040_ppm.h>
#define tx_pin 4  // Gpio for output PPM stream 
ppmChannels_t channels; // the array for channels value
int inc = 0;
ppmWriter myPPM_TX;
void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("Starting...");
  channels = myPPM_TX.begin(tx_pin); // initialyze a PPM signal, defaut 8 channels, default timing
  if (channels == NULL) {
    Serial.println("Error. No PIO ressources available ??");
  }
  else {
    Serial.println("PPM stream initialized.");
    myPPM_TX.start();
    Serial.println("PPM stream generated.");
    // update the channels values each 100ms. (1500us default value in the first burst) 
    while (true) {
      delay(100);
      for (int i = 1; i <= 8; i++) channels[i] = 1000 + inc * i;
      inc = (inc + 10) % 100;
    }
  }
}
void loop() {
}
