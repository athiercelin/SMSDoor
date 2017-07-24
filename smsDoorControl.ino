/*
    Written by Arnaud Thiercelin on Jul 23, 2017
    Derived from the sample code from Adafruit - see header below
    MIT license see License.md for more information
*/

/***************************************************
  This is an example for our Adafruit FONA Cellular Module

  Designed specifically to work with the Adafruit FONA
  ----> http://www.adafruit.com/products/1946
  ----> http://www.adafruit.com/products/1963
  ----> http://www.adafruit.com/products/2468
  ----> http://www.adafruit.com/products/2542

  These cellular modules use TTL Serial to communicate, 2 pins are
  required to interface
  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  Written by Limor Fried/Ladyada for Adafruit Industries.
  BSD license, all text above must be included in any redistribution
****************************************************/

#include "Adafruit_FONA.h"

/** Settings **/
// This host ID is to be set in order to identify the owner of this SMSDoor
#define hostCallerID "+1INSERTNUMBER"

// This code is what should be sent by your host to gain access.
// It should be long enough to be secure, short enough to be type easily.
#define entryCode "123456"

// when debug mode is active, the execution is driven by the serial interface with the arduino.
#define debugMode false

// when test mode is active, the host commands are ignored so you can send guest command from the host number
#define testMode false

/** Implementation **/
#define FONA_RX 2
#define FONA_TX 3
#define FONA_RST 4

// this is a large buffer for replies
char replybuffer[255];

// We default to using software serial. If you want to use hardware serial
// (because softserial isnt supported) comment out the following three lines
// and uncomment the HardwareSerial line
#include <SoftwareSerial.h>
SoftwareSerial fonaSS = SoftwareSerial(FONA_TX, FONA_RX);
SoftwareSerial *fonaSerial = &fonaSS;

// Hardware serial is also possible!
//  HardwareSerial *fonaSerial = &Serial1;

Adafruit_FONA fona = Adafruit_FONA(FONA_RST);

uint8_t readline(char *buff, uint8_t maxbuff, uint16_t timeout = 0);

// Relay control
int relayPin = 10;

void setup() {
  if (debugMode) {
    while (!Serial);
  }

  Serial.begin(115200);
  Serial.println(F("FONA SMS caller ID test"));
  Serial.println(F("Initializing....(May take 3 seconds)"));

  // make it slow so its easy to read!
  fonaSerial->begin(4800);
  if (! fona.begin(*fonaSerial)) {
    Serial.println(F("Couldn't find FONA"));
    while (1);
  }
  Serial.println(F("FONA is OK"));

  // Print SIM card IMEI number.
  char imei[16] = {0}; // MUST use a 16 character buffer for IMEI!
  uint8_t imeiLen = fona.getIMEI(imei);
  if (imeiLen > 0) {
    Serial.print("SIM card IMEI: "); Serial.println(imei);
  }

  Serial.println("FONA Ready");

  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, HIGH);
}

char fonaInBuffer[64];          //for notifications from the FONA

void loop() {

  char* bufPtr = fonaInBuffer;    //handy buffer pointer

  if (fona.available())      //any data available from the FONA?
  {
    int slot = 0;            //this will be the slot number of the SMS
    int charCount = 0;
    //Read the notification into fonaInBuffer
    do  {
      *bufPtr = fona.read();
      Serial.write(*bufPtr);
      delay(1);
    } while ((*bufPtr++ != '\n') && (fona.available()) && (++charCount < (sizeof(fonaInBuffer) - 1)));

    //Add a terminal NULL to the notification string
    *bufPtr = 0;

    //Scan the notification string for an SMS received notification.
    //  If it's an SMS message, we'll get the slot number in 'slot'
    if (1 == sscanf(fonaInBuffer, "+CMTI: \"SM\",%d", &slot)) {
      Serial.print("slot: "); Serial.println(slot);

      char callerIDbuffer[32];  //we'll store the SMS sender number in here

      memset(&callerIDbuffer, 0, 31);

      // Retrieve SMS sender address/phone number.
      if (! fona.getSMSSender(slot, callerIDbuffer, 31)) {
        Serial.println("Didn't find SMS message in slot!");
      }
      Serial.print(F("FROM: ")); Serial.println(callerIDbuffer);

      // Retrieve SMS value.
      uint16_t smslen;
      char messageBuffer[255];
      if (! fona.readSMS(slot, messageBuffer, 250, &smslen)) { // pass in buffer and max len!
        Serial.println("Failed reading SMS content!");
      } else {
        char *smsMessage = messageBuffer;

        smsMessage[smslen] = 0;
        // Check is callerID is Host
        if (strcmp(callerIDbuffer, hostCallerID) == 0 && testMode == false) {
          handleHostRequest(smsMessage);
        } else {
          handleGuestRequest(callerIDbuffer, smsMessage);
        }

        // delete the original msg after it is processed
        //   otherwise, we will fill up all the slots
        //   and then we won't be able to receive SMS anymore
        if (fona.deleteSMS(slot)) {
          Serial.println(F("SMS Deleted!"));
        } else {
          Serial.println(F("Couldn't delete SMS"));
        }
      }
    } else {
      Serial.print(F("Failed to get slot:"));
      Serial.println(slot);
    }
  }
}

void openDoor() {
  // Turn on pin
  digitalWrite(relayPin, LOW);

  // Wait 10s
  delay(10000);

  // Turn off pin
  digitalWrite(relayPin, HIGH);
}

/** Guest functions **/
void handleGuestRequest(char *callerID, char *smsCode) {
  // Check the message content for a valid key
  bool hasValidKey = false;

  Serial.print("SMS Received:");
  Serial.println(smsCode);

  Serial.print("FROM:");
  Serial.println(callerID);

  
  if (strcmp(smsCode, entryCode) == 0) {
    hasValidKey = true;
  } else {
    Serial.print("Failed code ");
    Serial.print(smsCode);
    Serial.print(" -> expected:");
    Serial.println(entryCode);
  }

  //Send back an automatic response based on key
  if (hasValidKey == true) {
    Serial.println("Sending Success Response...");
    if (!fona.sendSMS(callerID, "Access Granted!")) {
      Serial.println(F("Failed sending response"));
    } else {
      Serial.println(F("Sent!"));
    }
  } else {
    Serial.println("Sending Error Response...");
    if (!fona.sendSMS(callerID, "Access Denied.")) {
      Serial.println(F("Failed sending response"));
    } else {
      Serial.println(F("Sent!"));
    }
  }

  // Act on door based on key.
  if (hasValidKey == true) {
    openDoor();
    h_notifyAccess(true, callerID);
  } else {
    h_notifyAccess(false, callerID);
  }
}

/** Host Functions **/
void handleHostRequest(char *smsMessage) {
  char *cmd = strsep(&smsMessage, " ");
  char *value = smsMessage;

  Serial.print("Handling Host Command: ");
  Serial.print(cmd);
  Serial.print(", arg: ");
  Serial.println(value);

  if (!strcmp(cmd, "OVERRIDE")) { // Manual Access
    h_manualAccess();
  } else if (!strcmp(cmd, "FLUSHSMS")) {
    h_deleteAllSMS();
  } else {
    Serial.println("Unknownd Host Command");
    fona.sendSMS(hostCallerID, "Unknownd Host Command");
  }
}

// This functions notifies the host of entries.
void h_notifyAccess(bool access, char *guest) {
  if (access == true) {
    char *notifMessage = strcat("Access granted to: ", guest);

    Serial.println("Sending Success Response...");
    if (!fona.sendSMS(hostCallerID, notifMessage)) {
      Serial.println(F("Failed"));
    } else {
      Serial.println(F("Sent!"));
    }
  } else {
    char *notifMessage = strcat("Access denied to: ", guest);

    Serial.println("Sending Error Response...");
    if (!fona.sendSMS(hostCallerID, notifMessage)) {
      Serial.println(F("Failed"));
    } else {
      Serial.println(F("Sent!"));
    }
  }
}

void h_manualAccess() {
  char *notifMessage = "Manual Access Granted Sir.";
  Serial.println(notifMessage);
  if (!fona.sendSMS(hostCallerID, notifMessage)) {
    Serial.println(F("Failed"));
  } else {
    Serial.println(F("Sent!"));
  }
  openDoor();
}

void h_deleteAllSMS() {
  int slot = 0;

  for (; slot < 244; slot++) {
    if (fona.deleteSMS(slot)) {
      Serial.print(F("SMS Deleted at Slot:"));
      Serial.println(slot);
    }
  }
}


