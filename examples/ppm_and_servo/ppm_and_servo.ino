/*
ppm RX, ppm TX, servo
 
 In this example we:
 - generate a ppm stream with a ppmWriter on pin pin_TX
 - we feed back this ppm stream to pin_RX with a jumper
 - decode the ppm stream received on pin_RX with a ppmReader
 - send the received pulse to a RC servo (we use channel 1 in the ppm stream) connected to servo_pin
 - update in a loop the value of channel 1
 The servo moves slowly in a direction, with a fast return.

 Be careful when powering the servo motor: the current draw may be high. 
 Better to use a dedicated power supply for the servo.
 
 More information on the servo library http://arduino.cc/en/Tutorial/Sweep

*/

#include <Servo.h>
#include <RP2040_ppm.h>

#define rx_pin 4   // put a jumper between rx and tx pin.
#define tx_pin 3
#define servo_pin 2

Servo myservo;  // create servo object to control a servo

ppmReader myPPM_RX;
ppmWriter myPPM_TX;

ppmChannels_t ppmArrayRX;  // where we put the values to send
ppmChannels_t ppmArrayTX;  // where we receive the vlues.

void setup() {
  myservo.attach(servo_pin); 
  // initialize a 8 channels PPM writer, RISING style (in fact we only use channel 1 for the servo)
  ppmArrayTX = myPPM_TX.begin(tx_pin);
  ppmArrayRX = myPPM_RX.begin(rx_pin);
  myPPM_TX.start();
  myPPM_RX.start();
}
int pos;
void loop() {
  // each 30ms we update the value of the pulses sent by myPPM_TX
  //   end send to the servo the pulse received by myPPM_RX
  delay(30);
  ppmArrayTX[1] = 1000 + pos; // will be sent in the next ppm frames
  pos = (pos + 5) % 1000;
  myservo.writeMicroseconds(ppmArrayRX[1]);  // read the ppm stream and send pulse length to servo
}
