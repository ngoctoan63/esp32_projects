#include "Arduino.h"
#include <PubSubClient.h>
#include <Ticker.h>
#include <EEPROM.h>
#include <Ticker.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
// #include "libraries\md5\MD5.h"
#include <MD5.h>
#include <HTTPClient.h>
#include <Update.h>
#define DEBUG 1
#define SERVICE_UUID        "8c30f045-683a-4777-8d21-87def63e4ef5"
#define CHARACTERISTIC_UUID "e6eae575-4d89-4750-bf3e-c82d6a1cd299"
#define RECONNECT_TIME 3000
#define CH1_PIN 25
#define CH2_PIN 26
#define CH3_PIN 27
#define CH4_PIN 14
#define ON 1
#define OFF 0
#define STATUS_LED 2
#define BUTTON_IN 0
#define SENSOR1 22
#define SENSOR2 23
// #define MQTT_SERVER "mqtt.mujin24.com"
#define MQTT_SERVER "hassio.gpn-advantech.com"
#define MQTT_USERNAME "gdev"
#define MQTT_PASSWORD "gdev12345"
#define MQTT_PORT 1883

BLECharacteristic *pCharacteristic;
BLEServer *pServer;
BLEService *pService;

bool isAuthenticated = false;
bool isAppConnected = false;
bool isBleInited = false;
bool isAdvertisingStarted = false;
bool isSmartconfig = false;
bool longPress = false;
int press_count = 0;
unsigned long last_butoon_press_time;
String ssid;
String password;
String key;

unsigned long last_connect_time;
unsigned long check_time = 0;
unsigned long WifiConnect_time = 0;
String mqtt_topic_pub = "stat/";
String mqtt_topic_sub = "cmnd/";
String cmd = "";
String otaURL = "";
WiFiClient espClient;
PubSubClient client(espClient);
String myid = "Gswitch_";
unsigned long unlock_time = 0;
#pragma region input data
#pragma endregion
bool isHolding = false;
bool oSensor = false;
bool cSensor = false;
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(8, 8, 8, 8);
Ticker ticker;
struct {
	int isPaired;
	bool isStaticIp;
	uint8_t ip[4];
	uint8_t gateway[4];
} info;

void publish_status();
void tick();
void performOTA();
void publishSensorStatus(bool sValue, String subTopic);

class MyServerCallbacks: public BLEServerCallbacks {
	void onConnect(BLEServer *pServer) {
		isAppConnected = true;
		isAuthenticated = false;
		Serial.println(pServer->getConnId());
		Serial.println(">>Client Connected!");
		delay(200);
//		pServer->startAdvertising(); // for multi client
	}

	void onDisconnect(BLEServer *pServer) {
		isAppConnected = false;
		Serial.println(">>Client Disconnected!");
		pCharacteristic->setValue("disconnected");
		pCharacteristic->notify(); // Send the value to the app!
		delay(200);
		pServer->startAdvertising();
		delay(200);
	}
};
class MyCallbacks: public BLECharacteristicCallbacks {
	void onWrite(BLECharacteristic *pCharacteristic) {
		std::string rxValue = pCharacteristic->getValue(); // Get value as std::string
		String receivedData = String(rxValue.c_str());
		if (receivedData.length() > 0) {
//			Serial.println(F("*********"));
//			Serial.print(F("Received Value: "));
//			Serial.println(receivedData);
		}
		if (isAuthenticated == false) {
			if (receivedData.indexOf(key) != -1) {
				isAuthenticated = true;
			} else {
				return;
			}
		} else if (receivedData.startsWith("SSID")) {
			ssid = receivedData.substring(5);
//			Serial.print(F("SSID: "));
//			Serial.println(ssid);
		} else if (receivedData.startsWith("PASS")) {
			password = receivedData.substring(5);
//			Serial.print(F("Password: "));
//			Serial.println(password);
			WiFi.mode(WIFI_STA); // Set to station mode
			WiFi.begin(ssid.c_str(), password.c_str());
//			Serial.print(F("Connecting to Wi-Fi"));
			int i = 0;
			while (WiFi.status() != WL_CONNECTED) {
//				Serial.print(F("."));
				delay(1000);
				if (i++ > 20) {
//					Serial.println(F("Connection failed!"));
					break;
				}
			}
			if (WiFi.status() == WL_CONNECTED) {
//				Serial.println(F("Connected to Wi-Fi!"));
//				Serial.print(F("IP address: "));
//				Serial.println(WiFi.localIP());
			}
		}
	} //end onWrite();
};
void initInfo() {
	info.isPaired = 0;
	info.isStaticIp = 0;
	for (int i = 0; i < 4; i++) {
		info.ip[i] = 0;
		info.gateway[i] = 0;
	}
}
void writeInfo() {
	EEPROM.begin(128);
	EEPROM.put(0, info);
	EEPROM.commit();
	delay(50);
	EEPROM.end();
}
void readInfo() {
	EEPROM.begin(128);
	EEPROM.get(0, info);
#ifdef DEBUG
	Serial.print(F("\nisPaired:"));
	Serial.print(info.isPaired);
	Serial.print(F("\nMode:"));
	Serial.print(info.isStaticIp);
	Serial.print(F("\nStatic IP:"));
	for (int i = 0; i < 4; i++) {
		Serial.print(info.ip[i]);
		if (i < 3)
			Serial.print(F("."));
	}
	Serial.print(F("\nStatic GATEWAY:"));
	for (int i = 0; i < 4; i++) {
		Serial.print(info.gateway[i]);
		if (i < 3)
			Serial.print(F("."));
	}
#endif
	if (info.isPaired == -1) //esp chưa init
			{
		initInfo();
		writeInfo();
		ESP.restart();
	}
}
void stopBLEService() {
	if (isAppConnected) {
		pServer->disconnect(0);
	}
	isAdvertisingStarted = false;
	BLEAdvertising *pAdvertising = pServer->getAdvertising();
	pAdvertising->stop();
//	Serial.println(F("BLE Advertising Stopped!\n"));
}
void bleInit() {
//	BLEAdvertisementData adData;
//	adData.setName(myid.c_str());  // Đây mới là tên hiển thị khi scan
	if (!isBleInited) {
		isBleInited = true;
		BLEDevice::init(myid.c_str());
		Serial.println(F("BLE Enabled!"));
		pServer = BLEDevice::createServer();
		pServer->setCallbacks(new MyServerCallbacks());
// Create the BLE Service
		pService = pServer->createService(SERVICE_UUID);
// Create a BLE Characteristic
		pCharacteristic = pService->createCharacteristic(
		CHARACTERISTIC_UUID,
				BLECharacteristic::PROPERTY_READ
						| BLECharacteristic::PROPERTY_WRITE
						| BLECharacteristic::PROPERTY_NOTIFY);
		pCharacteristic->setCallbacks(new MyCallbacks());
		// Start the service
		pService->start();
		// Start advertising
		pServer->getAdvertising()->start();
		isAdvertisingStarted = true;
//		if (!isSmartconfig)
			ticker.attach(1.5, tick);
	} else {
		if (!isAdvertisingStarted) {
			// Start the service
			pService->start();
			// Start advertising
			pServer->getAdvertising()->start();
			isAdvertisingStarted = true;
			ticker.attach(1.5, tick);
		}
	}
}

void tick() {
	//toggle state
	int state = digitalRead(STATUS_LED);  // get the current state of GPIO1 pin
	digitalWrite(STATUS_LED, !state);     // set pin to the opposite state
}
void reconnect() {

	if (millis() - last_connect_time > RECONNECT_TIME) {
		last_connect_time = millis();
//		Serial.print(F("Attempting MQTT connection..."));
		if (client.connect(myid.c_str(), MQTT_USERNAME, MQTT_PASSWORD,
				(mqtt_topic_pub).c_str(), 1, true, "offline")) {
			client.publish((mqtt_topic_pub + "/WIFI").c_str(),
					WiFi.SSID().c_str(), true);
			client.publish((mqtt_topic_pub + "/IP").c_str(),
					WiFi.localIP().toString().c_str(), true);
			Serial.print(F("\nSSID: "));
			Serial.print(WiFi.SSID());
			Serial.print(F("\nIP:"));
			Serial.print(WiFi.localIP().toString());
			Serial.print(F("\nGATEWAY: "));
			Serial.println(WiFi.gatewayIP());
			client.publish((mqtt_topic_pub).c_str(), "online", true);
			publish_status();
			client.subscribe((mqtt_topic_sub + myid + "/#").c_str());
			publishSensorStatus(oSensor, "/sensor1");
			publishSensorStatus(cSensor, "/sensor2");
			ticker.detach();
			digitalWrite(STATUS_LED, 1);
		} else {
			Serial.print(F("failed, rc="));
			Serial.println(client.state());
		}
	}
}
String getid() {
	String temp = WiFi.macAddress();
	temp.replace(":", "");
	return temp;
}
void publish_status() {
	//CH1
	if (digitalRead(CH1_PIN))
		client.publish((mqtt_topic_pub + "/POWER1").c_str(), "ON", true);
	else
		client.publish((mqtt_topic_pub + "/POWER1").c_str(), "OFF", true);
	//CH2
#ifdef CH2_PIN
	if (digitalRead(CH2_PIN))
		client.publish((mqtt_topic_pub + "/POWER2").c_str(), "ON", true);
	else
		client.publish((mqtt_topic_pub + "/POWER2").c_str(), "OFF", true);
#endif
#ifdef CH3_PIN
	//CH3
	if (digitalRead(CH3_PIN))
		client.publish((mqtt_topic_pub + "/POWER3").c_str(), "ON", true);
	else
		client.publish((mqtt_topic_pub + "/POWER3").c_str(), "OFF", true);
#endif
#ifdef CH4_PIN
	//CH4
	if (digitalRead(CH4_PIN))
		client.publish((mqtt_topic_pub + "/POWER4").c_str(), "ON", true);
	else
		client.publish((mqtt_topic_pub + "/POWER4").c_str(), "OFF", true);
#endif
	publishSensorStatus(oSensor, "/sensor1");
	publishSensorStatus(cSensor, "/sensor2");
}
void callback(char *topic, byte *payload, unsigned int length) {
	String topic_str(topic);
	String data_str;
	for (int i = 0; i < length; i++) {
		data_str += (char) payload[i];
	}
	data_str[length] = '\0';
	cmd = (String) data_str;
	if (cmd.indexOf("update") == 0)
		performOTA();
	if (topic_str.indexOf("otaURL") != -1) {
		otaURL = cmd;
		Serial.println(otaURL);
	}
	if (topic_str.indexOf("POWER1") != -1) {
		if (cmd.indexOf("ON") != -1) {
			digitalWrite(CH1_PIN, ON);
		} else {
			digitalWrite(CH1_PIN, OFF);
		}
	}
#ifdef CH2_PIN
	if (topic_str.indexOf("POWER2") != -1) {
		if (cmd.indexOf("ON") != -1) {
			digitalWrite(CH2_PIN, ON);
		} else {
			digitalWrite(CH2_PIN, OFF);
		}
	}
#endif
#ifdef CH3_PIN
	if (topic_str.indexOf("POWER3") != -1) {
		if (cmd.indexOf("ON") != -1) {
			digitalWrite(CH3_PIN, ON);
		} else {
			digitalWrite(CH3_PIN, OFF);
		}
	}
#endif
#ifdef CH4_PIN
	if (topic_str.indexOf("POWER4") != -1) {
		if (cmd.indexOf("ON") != -1) {
			digitalWrite(CH4_PIN, ON);
		} else {
			digitalWrite(CH4_PIN, OFF);
		}
	}
#endif
	publish_status();
}
void performOTA() {
	ticker.attach(0.05, tick);
	HTTPClient http;
	http.begin(otaURL);
	int httpCode = http.GET();

	if (httpCode == HTTP_CODE_OK) {
		int contentLength = http.getSize();
		WiFiClient *stream = http.getStreamPtr();

		bool canBegin = Update.begin(contentLength);
		if (!canBegin) {
			client.publish((mqtt_topic_pub + "/updating").c_str(),
					"Not enough memory for OTA!", false);
			return;
		}
		int written = 0;
		uint8_t buff[128];
		unsigned long lastPercent = 0;

		while (http.connected() && written < contentLength) {
			size_t available = stream->available();
			if (available) {
				size_t read = stream->readBytes(buff,
						min(sizeof(buff), available));
				Update.write(buff, read);
				written += read;
				// Hiển thị %
				int percent = (written * 100) / contentLength;
				String strPecent = (String) percent;
				strPecent = strPecent + "%";
				if (percent != lastPercent) {
					client.publish((mqtt_topic_pub + "/updating").c_str(),
							strPecent.c_str(), false);
					lastPercent = percent;
				}
			}
			delay(1); // tránh watchdog
		}
		Serial.println();

		if (Update.end()) {
			if (Update.isFinished()) {
				ESP.restart();
			}
		}
	} else {
		client.publish((mqtt_topic_pub + "/updating").c_str(), "Failed!",
				false);
	}
	http.end();
}
void publishSensorStatus(bool sValue, String subTopic) {
	if (sValue) {
		client.publish((mqtt_topic_pub + subTopic).c_str(), "false", true);
	} else {
		client.publish((mqtt_topic_pub + subTopic).c_str(), "true", true);
	}
}
bool parseIPString(const String &input, uint8_t *output) {
	int parts[4];
	if (sscanf(input.c_str(), "%d.%d.%d.%d", &parts[0], &parts[1], &parts[2],
			&parts[3]) == 4) {
		for (int i = 0; i < 4; i++) {
			if (parts[i] < 0 || parts[i] > 255)
				return false;
			output[i] = (uint8_t) parts[i];
		}
		return true;
	}
	return false;
}

void setup() {
	Serial.begin(115200);
	pinMode(CH1_PIN, OUTPUT);
	digitalWrite(CH1_PIN, OFF);
#ifdef CH2_PIN
	pinMode(CH2_PIN, OUTPUT);
	digitalWrite(CH2_PIN, OFF);
#endif
#ifdef CH3_PIN
	pinMode(CH3_PIN, OUTPUT);
	digitalWrite(CH3_PIN, OFF);
#endif
#ifdef CH4_PIN
	pinMode(CH4_PIN, OUTPUT);
	digitalWrite(CH4_PIN, OFF);
#endif
	pinMode(STATUS_LED, OUTPUT);
	digitalWrite(STATUS_LED, 0);
	pinMode(BUTTON_IN, INPUT_PULLUP);
	pinMode(SENSOR1, INPUT_PULLUP);
	pinMode(SENSOR2, INPUT_PULLUP);

	readInfo();
	myid += getid();
	mqtt_topic_pub = mqtt_topic_pub + myid;
	Serial.println(F(""));
	Serial.println(mqtt_topic_pub);
	key = myid.substring(8, 18);
//	Serial.println(key);
	char *aus = new char[4]; //=key.c_str();
	strcpy(aus, key.c_str());
	//generate the MD5 hash for our string
	unsigned char *hash = MD5::make_hash(aus);
	//generate the digest (hex encoding) of our hash
	char *md5str = MD5::make_digest(hash, 16);
	free(hash);
	key = (String) md5str;
//	Serial.println(key);
	key = key.substring(10, 22);
	Serial.print(F("          "));
	Serial.println(key);
	client.setServer(MQTT_SERVER, MQTT_PORT);
	client.setCallback(callback);
	WiFi.setAutoReconnect(true);
	WiFi.mode(WIFI_STA);
	delay(3000);
	if (!digitalRead(BUTTON_IN)) {
		ticker.attach(0.1, tick);
		WiFi.begin();
		Serial.println(F("Begin SmartConfig"));
		int count = 0;
		WiFi.beginSmartConfig();
		while (WiFi.status() != WL_CONNECTED) {
			delay(500);
			count++;
			if (count % 20 == 0)
				Serial.print(F("\n"));
			Serial.print(F("."));
		}
		isSmartconfig = true;
		info.isPaired = 1;
		writeInfo();
		delay(50);
		ESP.restart();
	}
	if (info.isPaired == 0) {
		WiFi.begin();
		Serial.println(F(">>Init BLE"));
		bleInit();
	} else if (info.isPaired == 1) {
		WiFi.disconnect(false);
		delay(100);
		if (info.isStaticIp == 1)
			if (!WiFi.config(info.ip, info.gateway, subnet, dns)) {
				Serial.println("Static IP configuration failed!");
			} else {
				Serial.print("\nStatic IP:");
			}
		WiFi.begin();
		Serial.print(F("Connecting to Wi-Fi."));
		byte count = 0;
		while (WiFi.status() != WL_CONNECTED) {
			delay(500);
			Serial.print(F("."));
			count++;
			if (count > 20)
				break;
		}
	} else if (info.isPaired == 2) {
//		ticker.attach(0.1, tick);
//		WiFi.begin();
//		Serial.println(F("Begin SmartConfig"));
//		int count = 0;
//		WiFi.beginSmartConfig();
//		while (WiFi.status() != WL_CONNECTED) {
//			delay(500);
//			count++;
//			if (count % 20 == 0)
//				Serial.print(F("\n"));
//			Serial.print(F("."));
//		}
//		isSmartconfig = true;
//		info.isPaired = 1;
//		writeInfo();
//		delay(50);
//		ESP.restart();
	} else {
	}

}

void loop() {
	if (WiFi.status() == WL_CONNECTED) {
		if (!client.connected()) {
			ticker.attach(1, tick);
			reconnect();
		}
		client.loop();
		if (WifiConnect_time == 0)
			WifiConnect_time = millis();
		else {
			if (isAdvertisingStarted)
				if (millis() - WifiConnect_time > 30000)
					stopBLEService();
		}
		if (info.isPaired == 0) {
			info.isPaired = 1;
			writeInfo();
		}
		if (isSmartconfig)
			isSmartconfig = false;
	} else {
		if (!isSmartconfig)
			if (!isAdvertisingStarted) {
				bleInit();
			}
		if (WifiConnect_time != 0)
			WifiConnect_time = 0;
	}
	if (digitalRead(BUTTON_IN) == 0) {
		longPress = false;
		unsigned long press_time = millis();
		while (digitalRead(BUTTON_IN) == 0) {
			yield();
			if (millis() - press_time > 2000) {
				longPress = true;
				break;
			}
		}
		if (longPress) {
			Serial.print(F("\nLong press! "));
			Serial.println(press_count);
			if (press_count == 0) {
				if (!isSmartconfig) {
//					info.isPaired = 2;
//					writeInfo();
//					delay(50);
//					ESP.restart();
				}
			} else if (press_count == 2) { //unpair
				info.isPaired = 0;
				writeInfo();
				delay(50);
				ESP.restart();
			} else if (press_count == 3) { //init info
				initInfo();
				writeInfo();
				ESP.restart();
			} else if (press_count == 4) { //switch static ip mode
				digitalWrite(STATUS_LED, LOW);
				delay(500);
				info.isStaticIp = !info.isStaticIp;
				writeInfo();
				digitalWrite(STATUS_LED, HIGH);
				delay(1000);
				ESP.restart();
			}
			press_count = -1;
		} else {
			press_count++;
			last_butoon_press_time = millis();
		}
		if (press_count > 0) {
			Serial.print(F("Push button! "));
			Serial.println(press_count);
		}
	}
	if (press_count > 0)
		if (millis() - last_butoon_press_time > 2000) {
			press_count = 0;
			Serial.print(F("Push button! "));
			Serial.println(press_count);
		}
	if (press_count == 5) {
		WiFi.disconnect();
		WiFi.eraseAP();
		digitalWrite(STATUS_LED, 0);
		ESP.restart();
	}
	if (millis() - check_time > 1000) {
		check_time = millis();
		bool ss = digitalRead(SENSOR1);
		if (oSensor != ss) {
			oSensor = ss;
			publishSensorStatus(oSensor, "/sensor1");
		}
		ss = digitalRead(SENSOR2);
		if (cSensor != ss) {
			cSensor = ss;
			publishSensorStatus(cSensor, "/sensor2");
		}
	}
	if (Serial.available()) {
		Serial.setTimeout(200);
		String serial_mess = Serial.readString();
		String ssid, pass;

		if (serial_mess.indexOf("ip:") == 0) {
			serial_mess = serial_mess.substring(3);
			Serial.println(serial_mess);
			if (!parseIPString(serial_mess, info.ip)) {
				Serial.println(F("IP invalid!"));
			} else
				Serial.println(F("IP saved!"));
			writeInfo();
			return;
		}
		if (serial_mess.indexOf("gw:") == 0) {
			serial_mess = serial_mess.substring(3);
			Serial.println(serial_mess);
			if (!parseIPString(serial_mess, info.gateway)) {
				Serial.println(F("Gateway IP invalid!"));
			} else
				Serial.println(F("Gateway IP saved!"));
			writeInfo();
			return;
		}
		if (serial_mess.indexOf("stip:true") == 0) {
			info.isStaticIp = true;
			writeInfo();
			ESP.restart();
			return;
		}
		if (serial_mess.indexOf("stip:false") == 0) {
			info.isStaticIp = false;
			writeInfo();
			ESP.restart();
			return;
		}
//		if (serial_mess.indexOf("ssid") == 0) {
//			ssid = serial_mess.substring(5);
//			Serial.println("SSID:"+ssid);
//			return;
//		}
//		if (serial_mess.indexOf("pass") == 0) {
//			pass = serial_mess.substring(5);
//			Serial.println("PASS:"+pass);
//			WiFi.disconnect(false);
//			digitalWrite(STATUS_LED, LOW);
//			WiFi.disconnect(true);
//			delay(1000);
//			WiFi.mode(WIFI_OFF);
//			delay(100);
//			WiFi.mode(WIFI_STA);
//			WiFi.begin(ssid.c_str(), pass.c_str());
//			return;
//		}
	}
}

