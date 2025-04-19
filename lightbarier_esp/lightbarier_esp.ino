#include <DCF77.h>       //https://github.com/thijse/Arduino-DCF77
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// compile info Target ESP32 Devkitc ==> select DOIT ESP32 DEVKIT V1

//Defines
#define myLog(content) Serial.print(content)
#define myLogLn(content) Serial.println(content)

// Settings
static constexpr uint32_t DCF_INTERUPT_2_PIN{2};
static constexpr uint32_t DCF_INTERUPT_PIN{0};
static constexpr uint32_t LIGHTBARIER_PIN{15};
static constexpr uint32_t PIPE_PIN{16};

#define SERVICE_UUID        "723700b4-b295-42b3-9881-b4f0351e3569"
#define CHARACTERISTIC_UUID "3cc4b861-0818-4dcd-aac8-8c494539bf05"


//global variables
DCF77 DCF = DCF77(0,DCF_INTERUPT_PIN);
static struct timeval lastEventTime;
static bool enventTriggered{false};

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;
bool timeSynced = false;


//Lightbarier ISR
void IRAM_ATTR TimeEventLight()
{
  if(!enventTriggered)
  {
    gettimeofday(&lastEventTime, NULL);
    enventTriggered = true;
  }
}

//Pipe ISR
void IRAM_ATTR TimeEventPipe()
{
  if(!enventTriggered)
  {
    gettimeofday(&lastEventTime, NULL);
    enventTriggered = true;
  }
}

void IRAM_ATTR DCF77Event()
{
  time_t newTime = DCF.getTime();
  if (newTime != 0) {
    //sync
    timeval DCFtime {newTime,0};
    settimeofday(&DCFtime, nullptr);
    timeSynced = true;
  }
}

void printCurrentTime()
{
  char strftime_buf[64]{0};
  struct tm timeinfo;
  time_t now1;
  time(&now1);
  localtime_r(&now1, &timeinfo);  
  strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
  myLogLn(strftime_buf);
}

void printCurrentTime_us(struct timeval tv_now)
{
  int milli = tv_now.tv_usec / 1000;

  char buffer [80];
  strftime(buffer, 80, "%d.%m.%Y %H:%M:%S", localtime(&tv_now.tv_sec));

  char currentTime[84] = "";
  sprintf(currentTime, "%s,%03d", buffer, milli);
  myLogLn(currentTime);
}

void printCurrentTime_us()
{
  struct timeval tv_now;
  gettimeofday(&tv_now, NULL);
  printCurrentTime_us(tv_now);
}

class TimeNotificationServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      BLEDevice::startAdvertising();
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

void setupBLEServer()
{
  BLEDevice::init("ESP32");
  esp_err_t errRc=esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT,ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN ,ESP_PWR_LVL_P9); 

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new TimeNotificationServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_NOTIFY |
                      BLECharacteristic::PROPERTY_INDICATE
                    );

  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.client_characteristic_configuration.xml
  // Create a BLE Descriptor
  pCharacteristic->addDescriptor(new BLE2902());

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  myLogLn("Waiting a BLE client connection to notify...");
}


//init
void setup() {
  Serial.begin(115200); 

  //GIPO INIT
  pinMode(LIGHTBARIER_PIN,INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(LIGHTBARIER_PIN), TimeEventLight, FALLING);
  pinMode(PIPE_PIN,INPUT_PULLDOWN);
  attachInterrupt(digitalPinToInterrupt(PIPE_PIN), TimeEventPipe, RISING);

  // Set timezone to China Standard Time
  setenv("TZ", "CEST-1CET,M3.2.0/2:00:00,M11.1.0/2:00:00", 1);
  tzset();

  DCF.Start();
  attachInterrupt(digitalPinToInterrupt(DCF_INTERUPT_2_PIN), DCF77Event, RISING);

  setupBLEServer();
}
uint32_t lastEventTimestamp = millis();

void loop() {
  //time sync
  if(timeSynced)
  {
    timeSynced = false;
    myLog("Time is Syncroniced now :");
    printCurrentTime_us();
  }

  //time plotter
  if(millis() - lastEventTimestamp >= 1000)
  {
    lastEventTimestamp = millis();
    printCurrentTime_us();
  }
  
  //event lightbariere
  if(enventTriggered)
  {
    struct timeval lastTime = lastEventTime;
    if (deviceConnected) {
      pCharacteristic->setValue((uint8_t*)&lastTime, sizeof(lastTime));
      pCharacteristic->notify();
    }
    myLog("$$event$$: ");
    printCurrentTime_us(lastTime);

    //TODO verbessern
    delay(5000);
    noInterrupts();
    enventTriggered = false;
    interrupts();
  }

  //manage BLE Clients
  // disconnecting
  if (!deviceConnected && oldDeviceConnected) {
      delay(500); // give the bluetooth stack the chance to get things ready
      pServer->startAdvertising(); // restart advertising
      myLogLn("start advertising");
      oldDeviceConnected = deviceConnected;
  }
  // connecting
  if (deviceConnected && !oldDeviceConnected) {
      // do stuff here on connecting
      oldDeviceConnected = deviceConnected;
  }

}

