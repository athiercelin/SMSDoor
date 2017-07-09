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

/*
  THIS CODE IS STILL IN PROGRESS!

  Open up the serial console on the Arduino at 115200 baud to interact with FONA


  This code will receive an SMS, identify the sender's phone number, and automatically send a response

  For use with FONA 800 & 808, not 3G
*/

#include "Adafruit_FONA.h"

/** Settings **/
// This master ID is to be set in order to identify the owner of this SMSDoor
char *masterCallerID = "+1INSERTNUMBER";

// This code is what should be sent by your host to gain access.
// It should be long enough to be secure, short enough to be type easily.
char *entryCode = "123456";

// when debug mode is active, the execution is driven by the serial interface with the arduino.
bool debugMode = true;

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
  digitalWrite(relayPin, LOW);
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

        // Check is callerID is Master
        if (strcmp(callerIDbuffer, masterCallerID) == 0) {
          handleMasterRequest(smsMessage);
        } else {
          handleHostEntryRequest(callerIDbuffer, smsMessage);
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
  digitalWrite(relayPin, HIGH);

  // Wait 30s
  delay(3000);

  // Turn off pin
  digitalWrite(relayPin, LOW);
}

/** Host functions **/
void handleHostEntryRequest(char *callerIDbuffer, char *smsCode) {
  // Check the message content for a valid key
  bool hasValidKey = false;

  Serial.print("SMS Received:");
  Serial.println(smsCode);

  if (!strcmp(smsCode, entryCode)) {
    hasValidKey = true;
  }

  //Send back an automatic response based on key
  if (hasValidKey == true) {
    Serial.println("Sending Success Response...");
    if (!fona.sendSMS(callerIDbuffer, "Access Granted!")) {
      Serial.println(F("Failed sending response"));
    } else {
      Serial.println(F("Sent!"));
    }
  } else {
    Serial.println("Sending Error Response...");
    if (!fona.sendSMS(callerIDbuffer, "Access Denied.")) {
      Serial.println(F("Failed sending response"));
    } else {
      Serial.println(F("Sent!"));
    }
  }

  // Act on door based on key.
  if (hasValidKey == true) {
    openDoor();
    m_notifyAccess(true, callerIDbuffer);
  } else {
    m_notifyAccess(false, callerIDbuffer);
  }
}

/** Master Functions **/
void handleMasterRequest(char *smsMessage) {
  char *cmd = strsep(&smsMessage, " ");
  char *value = smsMessage;

  Serial.print("Handling Master Command: ");
  Serial.print(cmd);
  Serial.print(", arg: ");
  Serial.println(value);

  if (!strcmp(cmd, "UPDATECODE")) { // Update Code To Enter
    m_updateEntryCode(value);
  } else if (!strcmp(cmd, "OVERRIDE")) { // Manual Access
    m_manualAccess();
  } else {
    Serial.println("Unknownd Master Command");
    fona.sendSMS(masterCallerID, "Unknownd Master Command");
  }
}

// This functions notifies the master of entries.
void m_notifyAccess(bool access, char *host) {
  if (access == true) {
    char *notifMessage = strcat("Access granted to: ", host);

    Serial.println("Sending Success Response...");
    if (!fona.sendSMS(masterCallerID, notifMessage)) {
      Serial.println(F("Failed"));
    } else {
      Serial.println(F("Sent!"));
    }
  } else {
    char *notifMessage = strcat("Access denied to: ", host);

    Serial.println("Sending Error Response...");
    if (!fona.sendSMS(masterCallerID, notifMessage)) {
      Serial.println(F("Failed"));
    } else {
      Serial.println(F("Sent!"));
    }
  }
}

void m_updateEntryCode(char *newCode) {
  Serial.print("Code updated to: ");
  Serial.println(newCode);
  entryCode = newCode;
}

void m_manualAccess() {
  Serial.println("Manual Access Granted Sir.");
  openDoor();
}


