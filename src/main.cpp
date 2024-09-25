#define TINY_GSM_MODEM_SIM800 // Select your modem:
#define TINY_GSM_RX_BUFFER   1024  // Set RX buffer to 1Kb

#define SerialMon Serial // Set serial for debug console
#define SerialAT Serial2 // Set serial for AT commands (to the GSM module)


#define TINY_GSM_DEBUG SerialMon // Define the serial console for GSM debug prints
#define TINY_GSM_USE_GPRS true // Define how you're planning to connect to the internet.
// #define DUMP_AT_COMMANDS true

// set GSM SIM CARD PIN, if any
#define GSM_PIN ""


#include <TinyGsmClient.h>


#ifdef DUMP_AT_COMMANDS
  #include <StreamDebugger.h>
  StreamDebugger debugger(SerialAT, SerialMon);
  TinyGsm        modem(debugger);
#else
  TinyGsm        modem(SerialAT);
#endif

#define VALVE_RELAY_PIN 8 // pin 8 for  in1, pin 9 for in2
#define BUZZER_PIN 10 
#define PUSH_BUTTON_PIN 11
#define GSM_RESET_PIN 15

String ADMIN_NUMBER = "+254746287172";

unsigned long MAX_WATERING_TIME = 1 * 60 * 1000UL; // 1 minute
unsigned int WATERING_BEEP_INTERVAL = 2 * 1000; // 3 seconds

bool isWatering = false;
unsigned long startedWateringTime = 0;
unsigned long lastWateringBeepTime = 0;

TinyGsmClient client(modem);

void log_gsm_details(){

  String modemInfo = modem.getModemInfo();
  SerialMon.print("Modem Info: ");
  SerialMon.println(modemInfo);

  SerialMon.print("Voltage level: ");
  SerialMon.print(modem.getBattVoltage());  
  SerialMon.println("V");

  SerialMon.print("IMEI: ");
  SerialMon.println(modem.getIMEI());

  SerialMon.print("Signal quality: ");
  int result = modem.getSignalQuality();
  int network_strength = 0;
  if(result >= 2 && result <= 9) {
    //Signal Quality = Marginal
    network_strength = 1;
  }
  else if(result >= 10 && result <= 14) {
    //Signal Quality = OK
    network_strength = 2;
  }
  else if(result >= 15 && result <= 19) {
    //Signal Quality = Good
    network_strength = 3;
  }
  else if(result >= 20 && result <= 31) {
    //Signal Quality = Excellent
    network_strength = 4;
  }else if(result == 0 || result == 1 || result == 99){
    //not known or not detectable
    network_strength = 0;
  }   

  float signal_strength_percentage = (result / 31.0) * 100;
  SerialMon.print(signal_strength_percentage);
  SerialMon.print("% with ");
  SerialMon.print(network_strength);
  SerialMon.println(" bars");
}

void waterOnBeep() {
  for (int x = 0; x < 3; x++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(40);
    digitalWrite(BUZZER_PIN, LOW);
    delay(20);
  }
}
void waterOffBeep() {
  for (int x = 0; x < 2; x++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    delay(20);
  }
}

void wateringBeep() {
  for (int x = 0; x < 1; x++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    delay(20);
  }
}

void turnWaterOn(){
  digitalWrite(VALVE_RELAY_PIN, LOW);
  waterOnBeep();

  isWatering = true;
  startedWateringTime = millis();
}

void turnWaterOff(){
  digitalWrite(VALVE_RELAY_PIN, HIGH);
  waterOffBeep();

  isWatering = false;
  startedWateringTime = 0;
}

void playWateringBeep() {
  if (millis() - lastWateringBeepTime >= WATERING_BEEP_INTERVAL) {
    lastWateringBeepTime = millis();
    wateringBeep();
  }
  
}

void resetGsm() {
  Serial.println(F("RESETTING GSM..."));
  digitalWrite(GSM_RESET_PIN, LOW);
  delay(200);
  digitalWrite(GSM_RESET_PIN, HIGH);
  delay(2000);
}
void check_network(){
  if (!modem.isNetworkConnected()) {
    SerialMon.println("Network disconnected");
    if (!modem.waitForNetwork(180000L, true)) {
      SerialMon.println(" fail");
      delay(1000);
    }
    if (modem.isNetworkConnected()) {
      SerialMon.println("Network re-connected");
    }
  }
}

void sendSMS(String smsNumber, String smsText) {
  // Set preferred message format to text mode
  SerialAT.println("AT+CMGF=1");
  delay(10);
  SerialAT.println("AT+CMGS=\"" + smsNumber + "\"");
  delay(10);

  String response = "";
  unsigned long startTime = millis();
  while ((millis() - startTime) < 3000) {
    if (SerialAT.available()) {
      char c = SerialAT.read();
      response += c;
      if (response.endsWith(">")) {
        break;
      }
      // Reset the timeout when new data is received
      startTime = millis();
    }
  }
  SerialAT.println(smsText);
  delay(10);
  SerialAT.println((char)26);
  delay(100);
}


void processSMSCommands(String smsNumber, String smsDate, String smsText) {
  smsText.trim();
  smsText.toLowerCase();

  // acknowledge receipt of SMS
  if(smsText.length() > 10) {
    SerialMon.println("***SMS too long");
    return;
  }
  
  unsigned long wateringTime = millis() - startedWateringTime;
  if(startedWateringTime == 0)  wateringTime = 0;

  unsigned long wateringTimeMinutes = wateringTime / 1000 / 60;
  unsigned long wateringTimeSeconds = (wateringTime / 1000) % 60;

  if (smsText.startsWith("water on")) {
    turnWaterOn();

    SerialMon.println("Water Turned ON by " + smsNumber);
    sendSMS(smsNumber, "Water Turned ON");
  } else if (smsText.startsWith("water off")) {
    turnWaterOff();

    SerialMon.println("Water Turned OFF by " + smsNumber +"\nWatered for: " + String(wateringTimeMinutes) + " minutes and " + String(wateringTimeSeconds) + " seconds");
    sendSMS(smsNumber, "Water Turned OFF\nWatered for: " + String(wateringTimeMinutes) + " minutes and " + String(wateringTimeSeconds) + " seconds");
  }else if (smsText.startsWith("status")) {
    String status = (digitalRead(VALVE_RELAY_PIN) == HIGH) ? "OFF" : "ON";
    SerialMon.println("Water " + status + " status requested  by " + smsNumber);

    String message = "Water is currently turned " + status;
    if(isWatering) message += "\nBeen running for: " + String(wateringTimeMinutes) + " minutes and " + String(wateringTimeSeconds) + " seconds";
    sendSMS(smsNumber, message);
  }else if ( smsText.startsWith("stats")) {
    int8_t chargeState = 0;
    int8_t batLevel = 0;
    int16_t milliVolts = 0;
    modem.getBattStats(chargeState, batLevel, milliVolts);
    SerialMon.println("Battery stats requested  by " + smsNumber);
    String message = "Battery level: " + String(batLevel) + "%\nBattery voltage: " + String(milliVolts / 1000.0) + "V" + "\nBattery Charge State: " + String(chargeState) ;
    sendSMS(smsNumber, message);
  }
  
  else{
    SerialMon.println("Unknown command " + smsText + " by " + smsNumber);
    sendSMS(smsNumber, "Unknown command '" + smsText + "'\nPlease try again!");
  }
  
  
}

void parseSMS(String smsData) {
  // Split the response into lines
  int startIndex = 0;
  int endIndex = 0;
  String smsNumber = "";
  String smsDate = "";
  String smsText = "";

  while ((endIndex = smsData.indexOf('\n', startIndex)) >= 0) {
    String line = smsData.substring(startIndex, endIndex);
    startIndex = endIndex + 1;

    // Check if the line contains SMS information
    if (line.startsWith("+CMGL:")) {
      // +CMGL: 1,"REC READ","+254786689827","","24/09/22,19:49:21+12"
      int index1 = line.indexOf('"');          // Finds the first quotation mark
      int index2 = line.indexOf('"', index1 + 1);  // Finds the second quotation mark
      int index3 = line.indexOf('"', index2 + 1);  // Finds the third quotation mark (before the number)
      int index4 = line.indexOf('"', index3 + 1);  // Finds the fourth quotation mark (after the number)
      int index5 = line.indexOf('"', index4 + 1);  // Finds the fifth quotation mark
      int index6 = line.indexOf('"', index5 + 1);  // Finds the sixth quotation mark (after the date)
     
      smsNumber = line.substring(index3 + 1, index4);  // Extracts the SMS number
      smsDate = line.substring(index5 + 1, index6);    // Extracts the SMS date

      
    } else if (line.length() > 0) {
      smsText = line;

      if(smsNumber.length() > 0) {
        // Print the extracted values
        Serial.println(smsDate + " - " + smsNumber + " -> " + smsText + "\n");
        processSMSCommands(smsNumber, smsDate, smsText);
        return; //process only one sms and ignore the rest

        // reset the SMS number and date variables
        smsNumber = "";
        smsDate = "";
        smsText = "";
      }
      
    }
  }
}

void deleteAllSMS() {
  SerialMon.println("Deleting all SMS");
  SerialAT.println("AT+CMGF=1");  // Set SMS to text mode 
  delay(100);
  SerialAT.println("AT+CMGDA=\"DEL ALL\"");  // Delete all SMS AT+CMGDA="DEL ALL"
  delay(1000);
}

void checkUnreadSMS() {
  // SerialAT.println("AT+CMGF=1");  // Set SMS to text mode
  // delay(10);

  // Clear the buffer before sending a new command to SerialAT
  while (SerialAT.available()) {
    SerialMon.write(SerialAT.read());
  }

  SerialAT.println("AT+CMGL=\"REC UNREAD\"");  // Retrieve all unread messages AT+CMGL="REC UNREAD"
  // SerialAT.println("AT+CMGL=\"ALL\"");  // Retrieve all unread messages AT+CMGL="ALL"

  // Non-blocking response processing
  String message = "";
  unsigned long timeout = 3000;   // 3 seconds timeout
  unsigned long startTime = millis();
  bool okReceived = false;

  // Process the response data non-blocking
  while ((millis() - startTime) < timeout) {
    if (SerialAT.available()) {
      char c = SerialAT.read();
      message += c;

      // Check for end of response
      if (message.endsWith("OK\r\n")) {
        okReceived = true;
        // SerialMon.println("*OK received");
        break;
      }

      // Reset the timeout when new data is received
      startTime = millis();
    }
  }

  // Timeout handling
  if (!okReceived) {
    SerialMon.println("*Timeout occurred");
  }

  // SerialMon.println("Full response: " + String(message.length()) + " characters");
  // SerialMon.println(message+"\n===========\n");

  // Parse and display the individual SMS details
  deleteAllSMS();
  parseSMS(message);
}


void checkSMSStorage() {
  // <memr>, <memw> and <mems> to be used for reading, writing, and storing SMs.
  // +CPMS: <memr>,<usedr>,<totalr>,<memw>,<usedw>,<totalw>,<mems>,<useds>,<totals>
  // +CPMS: "SM_P",23     ,25      ,"SM_P",23     ,25      ,"SM_P",23     ,25  
  SerialAT.println("AT+CPMS?");  // Query current SMS storage
  delay(1000);
  
  while (SerialAT.available()) {
    String response = SerialAT.readStringUntil('\n');
    SerialMon.println(response);  // Print the response to the Serial Monitor
  }
}

void sendPowerOnSMS() {
  sendSMS(ADMIN_NUMBER, "Smart Irrigation System Powered On");
  SerialMon.println("Power On SMS sent");
  // if(smsSent){
  // }else{
  //   SerialMon.println("Power On SMS not sent");
  // }
}

void updateSerial() {
  String data = "";
  while (SerialAT.available()) {
    char c = SerialAT.read();
    SerialMon.write(c);

    data += c;
    if (data.indexOf("+CMTI:") != -1) {
      checkUnreadSMS();
      break;
    }
  }

  // Check if data is available on the Serial Monitor side
  if (SerialMon.available()) {
    String line = SerialMon.readStringUntil('\n');

    // while (SerialMon.available()) {
    // }

    SerialMon.println("Sending command: " + line);
    SerialAT.println(line);
    // checkUnreadSMS();
  }

  if(isWatering) playWateringBeep();
  if (digitalRead(PUSH_BUTTON_PIN) == HIGH) {
    if(isWatering) turnWaterOff();
    else turnWaterOn();
    
    while(digitalRead(PUSH_BUTTON_PIN) == HIGH) {}
  } 
}

void checkWateringTimeout() {
  if(!isWatering) return;

  unsigned long wateringTime = millis() - startedWateringTime;
  if(startedWateringTime == 0)  wateringTime = 0;

  unsigned long wateringTimeMinutes = wateringTime / 1000 / 60;
  unsigned long wateringTimeSeconds = (wateringTime / 1000) % 60;


  if (millis() - startedWateringTime >= MAX_WATERING_TIME) {
    // automatically stop watering
    turnWaterOff();


    SerialMon.println("Water Automatically Turned OFF by system \nWatered for: " + String(wateringTimeMinutes) + " minutes and " + String(wateringTimeSeconds) + " seconds");
    sendSMS(ADMIN_NUMBER, "Water Automatically Turned OFF\nWatered for: " + String(wateringTimeMinutes) + " minutes and " + String(wateringTimeSeconds) + " seconds");
  }
}

void setup() {
  // Set console baud rate
  SerialMon.begin(9600);
  // TinyGsmAutoBaud(SerialAT);
  SerialAT.begin(9600);
  delay(100);
  
  pinMode(VALVE_RELAY_PIN ,OUTPUT); 
  pinMode(GSM_RESET_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(PUSH_BUTTON_PIN, INPUT);

  digitalWrite(VALVE_RELAY_PIN, HIGH); // Normally ON Only For Chanies Relay Module 
  digitalWrite(GSM_RESET_PIN, HIGH);
  digitalWrite(BUZZER_PIN, LOW);


  SerialMon.println("\n\nStarting...");

  // Restart takes quite some time, To skip it, call init() instead of restart()
  SerialMon.println("Initializing modem...");
  // resetGsm();
  // modem.restart();
  // modem.init();


  // Unlock your SIM card with a PIN if needed
  if (GSM_PIN && modem.getSimStatus() != 3) { modem.simUnlock(GSM_PIN); }

  SerialMon.print("Waiting for network...");
  if (!modem.waitForNetwork(60000U, true)) {
    SerialMon.println(" fail");
    resetGsm();
  }else{
    SerialMon.println(" success");
  }

  if (modem.isNetworkConnected()) { SerialMon.println("Network connected"); }

  log_gsm_details();
  waterOnBeep();
  sendPowerOnSMS();


  checkSMSStorage();
  deleteAllSMS();
}

void loop() {
  checkWateringTimeout();
  updateSerial();

  delay(500);

}
