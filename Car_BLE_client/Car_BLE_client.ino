/**
 * A BLE client example that is rich in capabilities.
 * There is a lot new capabilities implemented.
 * author unknown
 * updated by chegewara
 */
#include <Wire.h>
#include <SPI.h>

#include <GxEPD.h>
#include <GxGDEH029A1/GxGDEH029A1.h>  // 2.9" b/w
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
#include <GxIO/GxIO.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>


#include "BLEDevice.h"

//compile info Target ESP32S Nodemuc ==> select NodeMUC-32S

//Defines
#define myLog(content) Serial.print(content)
#define myLogLn(content) Serial.println(content)

//--------------- pin defines ---------------
#define PN532_SCK (14)
#define PN532_MOSI (13)
#define PN532_SS (15)
#define PN532_MISO (12)

#define E_PAPER_SS (5)
#define E_PAPER_DC (17)
#define E_PAPER_RST (16)
#define E_PAPER_BUSY (4)

#define BEEP (27)

// The remote service we wish to connect to.
static BLEUUID serviceUUID("723700b4-b295-42b3-9881-b4f0351e3569");
// The characteristic of the remote service we are interested in.
static BLEUUID charUUID("3cc4b861-0818-4dcd-aac8-8c494539bf05");

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

//epaper globals
//E-paper
GxIO_Class io(SPI, E_PAPER_SS, E_PAPER_DC, E_PAPER_RST);
GxEPD_Class display(io, E_PAPER_RST, E_PAPER_BUSY);
bool newDataAvailable = false; 
struct timeval lastTime {};
struct timeval duration {};
struct timeval time_difference_to_even {};
struct timeval duration_difference_to_even {};

int currentMeasurementNumber = 0;
int currentDifference = 0;
long lastEpaperUpdate = 0;

// 'Symbol_Clock', 16x16px
const unsigned char epd_bitmap_Symbol_Clock [] PROGMEM = {
	0x07, 0xe0, 0x1e, 0x78, 0x31, 0x8c, 0x61, 0x84, 0x41, 0x82, 0xc1, 0x83, 0xc1, 0x83, 0x81, 0xf1, 
	0x81, 0xf1, 0xc0, 0x03, 0xc0, 0x03, 0x40, 0x02, 0x20, 0x04, 0x30, 0x0c, 0x1e, 0x78, 0x07, 0xe0
};
// 'Symbol_diference', 16x16px
const unsigned char epd_bitmap_Symbol_diference [] PROGMEM = {
	0x00, 0x00, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x03, 0xc0, 0x02, 0x40, 0x02, 0x40, 0x06, 0x60, 
	0x04, 0x20, 0x0c, 0x30, 0x0c, 0x30, 0x08, 0x10, 0x18, 0x18, 0x10, 0x08, 0x1f, 0xf8, 0x3f, 0xfc
};



struct timeval calcNextEvenTime(const struct timeval* unevenTime)
{
  struct tm nextEvenTime_tm = *localtime(&unevenTime->tv_sec);
  const uint32_t roundValue {10};
  nextEvenTime_tm.tm_sec = (int)round((float)nextEvenTime_tm.tm_sec / roundValue)*roundValue;

  if(nextEvenTime_tm.tm_sec >= 60)
  {
      nextEvenTime_tm.tm_sec-= 60;
      nextEvenTime_tm.tm_min += 1;
  }

  struct timeval nextEvenTime {};
  nextEvenTime.tv_sec = mktime(&nextEvenTime_tm);
  return nextEvenTime;
}

struct timeval differenceToNextEvenTime(const struct timeval& time)
{
  //time difference to next even time
  struct timeval nextEvenTime {calcNextEvenTime(&time)};
  struct timeval differenceToNextEvenTime{};

  timersub(&time, &nextEvenTime, &differenceToNextEvenTime);
  if(differenceToNextEvenTime.tv_sec < 0)
  {
    differenceToNextEvenTime.tv_usec = 1000000 - differenceToNextEvenTime.tv_usec;
  }
  return differenceToNextEvenTime;
}

void printCurrentTime_us(struct timeval tv_now, struct timeval tv_difference, struct timeval tv_duration) {
  int milli = tv_now.tv_usec / 1000;

  char buffer[80];
  strftime(buffer, 80, "now: %d.%m.%Y %H:%M:%S", localtime(&tv_now.tv_sec));

  char currentTime[84] = "";
  sprintf(currentTime, "%s,%03d", buffer, milli);
  myLogLn(currentTime);

  char differencebuffer[80];
  sprintf(differencebuffer, "dif: %ld,%ld", (long)tv_difference.tv_sec, (long)tv_difference.tv_usec);
  myLogLn(differencebuffer);

  char durationbuffer[80];
  strftime(durationbuffer, 80, "dur: %d.%m.%Y %H:%M:%S", localtime(&tv_duration.tv_sec));
  char duration[84] = "";
  int durationmilli = tv_duration.tv_usec / 1000;
  sprintf(duration, "%s,%03d", durationbuffer, durationmilli);
  myLogLn(duration);
}

static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
  digitalWrite(BEEP, true);
  delay(200);
  digitalWrite(BEEP, false);

  //time measurement
  struct timeval newTime = *(struct timeval*)pData;
  //time duration sinc last measurement
  if (lastTime.tv_usec != 0 && lastTime.tv_sec != 0) {
    timersub(&newTime, &lastTime, &duration);
  }
  time_difference_to_even = differenceToNextEvenTime(newTime);
  duration_difference_to_even = differenceToNextEvenTime(duration);

  lastTime = newTime;
  currentMeasurementNumber++;
  printCurrentTime_us(lastTime, time_difference_to_even, duration);

  newDataAvailable = true;
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("onDisconnect");
  }
};

bool connectToServer() {
  Serial.print("Forming a connection to ");
  Serial.println(myDevice->getAddress().toString().c_str());

  BLEClient* pClient = BLEDevice::createClient();
  Serial.println(" - Created client");

  pClient->setClientCallbacks(new MyClientCallback());

  // Connect to the remove BLE Server.
  pClient->connect(myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
  Serial.println(" - Connected to server");
  pClient->setMTU(517);  //set client to request maximum MTU from server (default is 23 otherwise)

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our service");


  // Obtain a reference to the characteristic in the service of the remote BLE server.
  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(charUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our characteristic");

  // Read the value of the characteristic.
  if (pRemoteCharacteristic->canRead()) {
    String value = pRemoteCharacteristic->readValue();
    Serial.print("The characteristic value was: ");
    //Serial.println(value);
  }
  Serial.println(" - characteristic check finished");
  if (pRemoteCharacteristic->canNotify())
    pRemoteCharacteristic->registerForNotify(notifyCallback);

  connected = true;
  return true;
}
/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {

      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = true;

    }  // Found our server
  }    // onResult
};     // MyAdvertisedDeviceCallbacks


void setup() {
  Serial.begin(115200);
  Serial.println("Starting Arduino BLE Client application...");

  //GPIO
  pinMode(BEEP, OUTPUT);

  display.init();
  display.eraseDisplay();
  showSetupScreen();
  delay(1000);


  //BLE init & Powersettings
  BLEDevice::init("");
  esp_err_t errRc = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9);


  showBLEDisconected();

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
}  // End of setup.


// This is the Arduino main loop function.
void loop() {

  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are
  // connected we set the connected flag to be true.
  if (doConnect == true) {
    if (connectToServer()) {
      Serial.println("We are now connected to the BLE Server.");
      showBLEConected();
    } else {
      Serial.println("We have failed to connect to the server; there is nothin more we will do.");
      showBLEFailed();
    }
    doConnect = false;
  }

  // If we are connected to a peer BLE Server, update the characteristic each time we are reached
  // with the current time since boot.
  if (connected) {
    if (newDataAvailable) {
      showMeasurementScreen(currentMeasurementNumber, lastTime, time_difference_to_even, duration, duration_difference_to_even);
      newDataAvailable = false;
    }
  } else if (doScan) {
    showBLEDisconected();
    BLEDevice::getScan()->start(0);  // this is just example to start scan after disconnect, most likely there is better way to do it in arduino
  }

  delay(1000);  // Delay a second between loops.
}  // End of loop


void showSetupScreen() {
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(0, 0);
  display.setFont(&FreeSans12pt7b);
  display.println();
  display.println("Rallye App");
  display.setFont(&FreeSans9pt7b);
  display.println();
  display.println("startet...");
  display.update();
}

void showBLEDisconected() {
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(0, 0);
  display.setFont(&FreeSans12pt7b);
  display.println();
  display.println("Rallye App");
  display.setFont(&FreeSans9pt7b);
  display.println();
  display.println("Verbinde mit");
  display.println("Lichtschranke");
  display.println("...");
  display.update();
}

void showBLEConected() {
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(0, 0);
  display.setFont(&FreeSans12pt7b);
  display.println();
  display.println("Rallye App");
  display.setFont(&FreeSans9pt7b);
  display.println();
  display.println("Lichtschranke");
  display.println("verbunden");
  display.update();
}

void showBLEFailed() {
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(0, 0);
  display.setFont(&FreeSans12pt7b);
  display.println();
  display.println("Rallye App");
  display.setFont(&FreeSans9pt7b);
  display.println();
  display.println("Fehlerhafte");
  display.println("Verbindung");
  display.println("mit Lichtschranke");
  display.update();
}
void displayTimeAndDiference(struct timeval& time,struct timeval& diference)
{
  display.setFont(&FreeSans9pt7b);

  display.drawBitmap(display.getCursorX(), display.getCursorY() - 14, epd_bitmap_Symbol_Clock, 16, 16, GxEPD_BLACK);
  display.setCursor(display.getCursorX()+20, display.getCursorY());

  char time_h_m_s_char[20];
  strftime(time_h_m_s_char, sizeof(time_h_m_s_char), "%H:%M:%S", localtime(&time.tv_sec));
  int milli = time.tv_usec / 1000;
  display.printf("%s,%03d\n", time_h_m_s_char, milli);

  display.setFont(&FreeMonoBold12pt7b);
  display.drawBitmap(display.getCursorX(), display.getCursorY() - 16, epd_bitmap_Symbol_diference, 16, 16, GxEPD_BLACK);
  display.setCursor(display.getCursorX()+20, display.getCursorY());
  if(diference.tv_sec >= 0)
  {
    display.print("+");
    display.printf("%ld,%03ld\n",(int32_t)diference.tv_sec, (int32_t)(diference.tv_usec/1000));
  }
  else if(diference.tv_sec < 0)
  {
     //bei negativen zahlen muss sekundenwert +1 gerechnet werden
    if(diference.tv_sec == -1)
    {
      //spezialfall da sonst zwischen -0,001 und -0,999s kein - angezeigt wird
      display.print("-");
    }
    display.printf("%ld,%03ld\n",(int32_t)diference.tv_sec + 1, (int32_t)(diference.tv_usec/1000));
  }
}

void showMeasurementScreen(int measurementNbr, struct timeval drivethroughTime, struct timeval drivethroughDifference, struct timeval durationTime, struct timeval durationDifference) {
  display.eraseDisplay();
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(0, 0);
  display.setFont(&FreeSans12pt7b);
  display.println();
  display.println("Rallye App");
  display.setFont(&FreeSans9pt7b);
  display.printf("Nummer: %d\n", measurementNbr);
  display.println();
  //time
  display.setFont(&FreeSans9pt7b);
  display.println("Durchfahrt:");
  displayTimeAndDiference(drivethroughTime, drivethroughDifference);

  //duration
  display.setFont(&FreeSans9pt7b);
  display.println();
  display.println("Intervall:");
  displayTimeAndDiference(durationTime, durationDifference);
  display.update();
}
