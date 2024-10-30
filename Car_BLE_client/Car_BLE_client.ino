/**
 * A BLE client example that is rich in capabilities.
 * There is a lot new capabilities implemented.
 * author unknown
 * updated by chegewara
 */
#include <Wire.h>
#include <SPI.h>

#include <GxEPD.h>
#include <GxGDEH029A1/GxGDEH029A1.h>      // 2.9" b/w
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
#include <GxIO/GxIO.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>


#include "BLEDevice.h"


//Defines
#define myLog(content) Serial.print(content)
#define myLogLn(content) Serial.println(content)

//--------------- pin defines ---------------
#define PN532_SCK    (14)
#define PN532_MOSI   (13)
#define PN532_SS     (15)
#define PN532_MISO   (12)

#define E_PAPER_SS   (5)
#define E_PAPER_DC   (17)
#define E_PAPER_RST  (16)
#define E_PAPER_BUSY (4)

#define BEEP (27)

// The remote service we wish to connect to.
static BLEUUID serviceUUID("723700b4-b295-42b3-9881-b4f0351e3569");
// The characteristic of the remote service we are interested in.
static BLEUUID    charUUID("3cc4b861-0818-4dcd-aac8-8c494539bf05");

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

//epaper globals
//E-paper
GxIO_Class io(SPI, E_PAPER_SS, E_PAPER_DC, E_PAPER_RST);
GxEPD_Class display(io, E_PAPER_RST, E_PAPER_BUSY);
bool newDataAvailable= false;
struct timeval lastTime = (struct timeval){ 0 };
struct timeval duration = (struct timeval){ 0 };
int currentMeasurementNumber=0;
int currentDifference = 0;
long lastEpaperUpdate = 0;

int timeval_subtract(struct timeval *result, struct timeval *x, struct timeval *y)
{
  struct timeval xx = *x;
  struct timeval yy = *y;
  x = &xx; y = &yy;

  if (x->tv_usec > 999999)
  {
    x->tv_sec += x->tv_usec / 1000000;
    x->tv_usec %= 1000000;
  }

  if (y->tv_usec > 999999)
  {
    y->tv_sec += y->tv_usec / 1000000;
    y->tv_usec %= 1000000;
  }

  result->tv_sec = x->tv_sec - y->tv_sec;

  if ((result->tv_usec = x->tv_usec - y->tv_usec) < 0)
  {
    result->tv_usec += 1000000;
    result->tv_sec--; // borrow
  }

  return result->tv_sec < 0;
}

void printCurrentTime_us(struct timeval tv_now, struct timeval tv_duration)
{
  int milli = tv_now.tv_usec / 1000;

  char buffer [80];
  strftime(buffer, 80, "%d.%m.%Y %H:%M:%S", localtime(&tv_now.tv_sec));

  char currentTime[84] = "";
  sprintf(currentTime, "%s,%03d", buffer, milli);
  myLogLn(currentTime);

  char durationbuffer [80];
  strftime(durationbuffer, 80, "%d.%m.%Y %H:%M:%S", localtime(&tv_now.tv_sec));
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
    
    struct timeval newTime = *(struct timeval*)pData;
    if (lastTime.tv_usec != 0 && lastTime.tv_sec != 0)
    {
      timeval_subtract(&duration, &newTime, &lastTime);
    }
    lastTime = newTime;
    currentMeasurementNumber++;
    printCurrentTime_us(lastTime, duration);
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
    
    BLEClient*  pClient  = BLEDevice::createClient();
    Serial.println(" - Created client");

    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remove BLE Server.
    pClient->connect(myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
    Serial.println(" - Connected to server");
    pClient->setMTU(517); //set client to request maximum MTU from server (default is 23 otherwise)
  
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
    if(pRemoteCharacteristic->canRead()) {
      String value = pRemoteCharacteristic->readValue();
      Serial.print("The characteristic value was: ");
      //Serial.println(value);
    }
    Serial.println(" - characteristic check finished");
    if(pRemoteCharacteristic->canNotify())
      pRemoteCharacteristic->registerForNotify(notifyCallback);

    connected = true;
    return true;
}
/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
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

    } // Found our server
  } // onResult
}; // MyAdvertisedDeviceCallbacks


void setup() {
  Serial.begin(115200);
  Serial.println("Starting Arduino BLE Client application...");

  //GPIO
  pinMode(BEEP, OUTPUT);

  display.init();
  display.eraseDisplay();
  showSetupScreen(&FreeMonoBold9pt7b);
  delay(1000);

  
  //BLE init & Powersettings
  BLEDevice::init("");
  esp_err_t errRc=esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT,ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN ,ESP_PWR_LVL_P9); 


  showBLEDisconected(&FreeMonoBold9pt7b);

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
} // End of setup.


// This is the Arduino main loop function.
void loop() {

  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are 
  // connected we set the connected flag to be true.
  if (doConnect == true) {
    if (connectToServer()) {
      Serial.println("We are now connected to the BLE Server.");
      showBLEConected(&FreeMonoBold9pt7b);
    } else {
      Serial.println("We have failed to connect to the server; there is nothin more we will do.");
      showBLEFailed(&FreeMonoBold9pt7b);
    }
    doConnect = false;
  }

  // If we are connected to a peer BLE Server, update the characteristic each time we are reached
  // with the current time since boot.
  if (connected) {
    if (newDataAvailable)
    {
      showMeasurementScreen(&FreeMonoBold12pt7b, currentMeasurementNumber, lastTime, duration);
      newDataAvailable = false;
    }
  }else if(doScan){
    showBLEDisconected(&FreeMonoBold9pt7b);
    BLEDevice::getScan()->start(0);  // this is just example to start scan after disconnect, most likely there is better way to do it in arduino
  }
  
  delay(1000); // Delay a second between loops.
} // End of loop


void showSetupScreen(const GFXfont* f)
{
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display.setFont(f);
  display.setCursor(0, 0);
  display.println();
  display.print("Rally ESP");
  display.println();
  display.println("startet...");
  display.update();
}

void showBLEDisconected(const GFXfont* f)
{
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display.setFont(f);
  display.setCursor(0, 0);
  display.println();
  display.print("Rally ESP");
  display.println();
  display.println("no BLE");
  display.println("try to connect");
  display.println("...");
  display.update();
}

void showBLEConected(const GFXfont* f)
{
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display.setFont(f);
  display.setCursor(0, 0);
  display.println();
  display.print("Rally ESP");
  display.println();
  display.println("BLE");
  display.println("connected");
  display.update();
}

void showBLEFailed(const GFXfont* f)
{
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display.setFont(f);
  display.setCursor(0, 0);
  display.println();
  display.print("Rally ESP");
  display.println();
  display.println("BLE");
  display.println("failed");
  display.update();
}

void showMeasurementScreen(const GFXfont* f, int measurementNbr, struct timeval drivethroughTime, struct timeval durationTime)
{
  display.eraseDisplay();
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display.setFont(f);
  display.setCursor(0, 0);
  display.println();
  display.println("Messung:");
  display.println(measurementNbr);
  display.println();
  display.println();
  //time
  display.println("um:");
  char time_char[20];
  strftime(time_char, sizeof(time_char), "%H:%M:%S", localtime(&drivethroughTime.tv_sec));
  display.print(time_char);
  display.print(",");
  int milli = drivethroughTime.tv_usec / 1000;
  char currentTime[20] = "";
  sprintf(currentTime, "%03d", milli);
  display.println(currentTime);
  //duration
  display.println("delta:");
  char duration_char[20];
  strftime(duration_char, sizeof(duration_char), "%H:%M:%S", localtime(&durationTime.tv_sec));
  display.print(duration_char);
  display.print(",");
  int durationmilli = durationTime.tv_usec / 1000;
  char durationTime_char[20] = "";
  sprintf(durationTime_char, "%03d", durationmilli);
  display.println(durationTime_char);
  display.update();
}

