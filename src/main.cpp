#include <Arduino.h>
#include <SPI.h>

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

#include <FastLED.h>

#include <Ultrasonic.h>

#include <PubSubClient.h>

#include <MFRC522.h>

#include <ArduinoJson.h>

#include <Servo.h>

#include <LittleFS.h>

//RFID RC522 PINS
#define RST_PIN D9
#define SDA_PIN D10
#define MOSI_PIN D11
#define MISO_PIN D12
#define SCK_PIN D13

//Addressable RGB PINS
#define LED_PIN D4

//HC-SR04 PINS
#define ECHO_PIN D2
#define TRIG_PIN D3

//Servo PIN
#define SERVO_PIN D8

//Addressable RGB DATAS
#define COLOR_ORDER GRB
#define CHIPSET WS2812

//RFID RC522 DATAS
#define POS 8
#define SIZE 16
#define ROWTOREAD 1

//Servo DATAS
#define SERVO_OPEN 0
#define SERVO_CLOSE 90

//HC-SR04 vars
Ultrasonic hcsr04(TRIG_PIN,ECHO_PIN);
int counter = 0;
int sum = 0;
int before = 0;
int after = 0;

//Addressable RGB vars
CRGB leds[1];

//Servo vars
Servo door;

//RFID RC522 vars
MFRC522 mfrc522(SDA_PIN, RST_PIN);
MFRC522::MIFARE_Key key;

bool rfid_tag_present_prev = false;
bool rfid_tag_present = false;
int _rfid_error_counter = 0;
bool _tag_found = false;

const byte sector		 = POS / 4;
const byte blockAddr	  = POS;
const byte trailerBlock   = (POS / 4) * 4 + 3;

//Json
StaticJsonDocument<100> json;

//Wifi connection
// const char* ssid = "Casellati Wifi";
// const char* password = "cuq98lcvomu4d";
WiFiClient client;

//Rest WS vars
//const char* server = "aac74be238fb64ab53f91b6eb5ebc154.balena-devices.com";
const int rest_port = 80;
const char* user_rest_path = "/users/";
const char* bins_rest_path = "/waste_bins/";
const bool ssl = false;

//EEPROM

String ssid;
String password;
String server;
String uuid;


//Functions
void print_byte_array(byte *buffer, byte bufferSize);
int read_with_no_error(byte start, byte end, byte **buffer, byte size_in_index);

void setGreen();
void setRed();
void setYellow();
void setOff();
void setWhite();

void error();

void openGate();
void closeGate();
char * hex2str(byte dataBlock[]);
void setup() {
	Serial.begin(9600);
	Serial.println();
	Serial.println("Progetto Industrial IoT");
	Serial.println("di Paolo Casellati e Fabio Bedeschi");
	Serial.println("Prototipo di bidone della spazzatura");
	Serial.println("---------------------------------------------");
	
	if (! LittleFS.begin()) {
		Serial.println("Formatting...");
		LittleFS.format();
	}

	// File f = LittleFS.open("/data.txt", "w+");
	// f.println("aac74be238fb64ab53f91b6eb5ebc154.balena-devices.com");
	// f.println("08090A0B0C0D0E0FFFFFFFFFFFFFFFFF");
	// f.println("Casellati Wifi");
	// f.println("cuq98lcvomu4d");
	// f.close();

	File f = LittleFS.open("/data.txt", "r");
	Serial.println();
	server = f.readStringUntil('\n');
	uuid = f.readStringUntil('\n');
	ssid = f.readStringUntil('\n');
	password = f.readStringUntil('\n');
	
	server.trim();
	uuid.trim();
	ssid.trim();
	password.trim();
		
	LittleFS.end();

	SPI.begin();		
	mfrc522.PCD_Init(); 
	delay(4);
	mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);
	
	FastLED.addLeds<NEOPIXEL, LED_PIN>(leds,1);
	FastLED.setBrightness(10);
	
	door.attach(SERVO_PIN);

	Serial.print("Connessione alla WiFi");
	WiFi.begin(ssid.c_str(),password.c_str());
	while (WiFi.status() != WL_CONNECTED) {
		Serial.print(".");
		delay(250);
	}
	Serial.println();
	Serial.print("Connesso a ");
	Serial.print(ssid);
	Serial.println(" correttamente.");

	//TODO: Scaricare l'aggiornamento degli indirizzi

	setGreen();
	
	//Password 
	for (byte i = 0; i < 6; i++) {
		key.keyByte[i] = 0xFF;
	}

}

char * hex2str(byte dataBlock[]) {
	char * str = (char *) malloc(33*sizeof(char));
	int i = 0;
	for (char * tmp = str; tmp < str + 32*sizeof(char); tmp += 2, i++) {
		sprintf(tmp,"%02X",dataBlock[i]);
	}
	return str;
}

void on_detect() {
	Serial.println("Riconosciuto TAG...");
	setWhite();

	byte **buffer = (byte **) malloc(ROWTOREAD * sizeof(byte *));
	Serial.println(F("Auth con chiave A..."));
	if (mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, &(mfrc522.uid)) == MFRC522::STATUS_OK) {
		if (read_with_no_error(blockAddr, trailerBlock, buffer, ROWTOREAD) == 0) {
			if (WiFi.isConnected()) {
				HTTPClient http;

				http.begin(client,server,rest_port, (user_rest_path) + String(hex2str(buffer[0])),ssl);
				int httpCode = http.GET();
				
				if (httpCode > 0) {
					http.end();
					Serial.println(httpCode);
					if (httpCode == HTTP_CODE_OK) {
						counter = 0;
						sum = 0;
						while (counter < 5) {
							int tmp =hcsr04.read();
							if (tmp >1 && tmp < 200 ) {
								sum += tmp;
								counter++;
							}
							Serial.println(tmp);
							delay(200);
						}
						before = sum/counter;


						openGate();
						setYellow();

						unsigned long currentMillis = millis();
						while (millis() < currentMillis + 10000){
							delay(10);
						}

						closeGate();

						counter = 0;
						sum = 0;
						while (counter < 5) {
							int tmp =hcsr04.read();
							if (tmp >1 && tmp < 200 ) {
								sum += tmp;
								counter++;
							}
							Serial.println(tmp);
							delay(200);
						}
						after = sum/counter;
						json["delta"] = after - before;
						String out;
						serializeJson(json, out);
						Serial.println(out);
						http.begin(client,server,rest_port, (user_rest_path) + String(hex2str(buffer[0])),ssl);
						http.addHeader("Content-Type", "application/json");
						httpCode = http.PATCH(out);
						if (httpCode > 0) {
							Serial.println(httpCode);
						} else {
							Serial.println(http.errorToString(httpCode).c_str());
						}
						http.end();
						json.clear();
						out = "";

						json["fill_level"] = after;
						serializeJson(json, out);
						Serial.println(out);
						http.begin(client,server,rest_port, (bins_rest_path) + String(hex2str(buffer[0])),ssl);
						http.addHeader("Content-Type", "application/json");
						httpCode = http.PATCH(out);
						if (httpCode > 0) {
							Serial.println(httpCode);
						} else {
							Serial.println(http.errorToString(httpCode).c_str());
						}
						http.end();
						json.clear();

					} else {
						error();
					}
				} else {
					Serial.println(http.errorToString(httpCode).c_str());
					http.end();
					error();
				}	
			} else {
				error();
			}

		} else {
			
			error();
		}
		
	} else {
			
		error();
	}
	mfrc522.PCD_StopCrypto1();
	mfrc522.PICC_HaltA();
	mfrc522.PCD_Reset();
	delay(100);
	mfrc522.PCD_Init();

}

void on_detach() {
	setGreen();
}

void print_byte_array(byte *buffer, byte bufferSize) {
	for (byte i = 0; i < bufferSize; i++) {
		Serial.print(buffer[i] < 0x10 ? " 0" : " ");
		Serial.print(buffer[i], HEX);
	}
}

int read_with_no_error(byte start, byte end, byte **buffer, byte size_in_index) {
	byte size = SIZE+2;
	byte i = 0;
	MFRC522::StatusCode status;
	int errors = 0;
	byte b[size];
	memset(b,0,size);
	for (byte i = 0; i < size_in_index; i++) {
		buffer[i] = (byte *) malloc(SIZE * sizeof(byte));
	}
	
	for (i = start; i < start + size_in_index; i++) {
		//status = MFRC522::STATUS_ERROR;
		status = (MFRC522::StatusCode) mfrc522.MIFARE_Read(i, b , &size);
		if (status != MFRC522::STATUS_OK) {
			errors++;
		}
		memcpy(buffer[i-start],b,SIZE);
	}
	return errors;
}

void setGreen() {
	// setColor(LOW,HIGH,LOW);
	leds[0] = CRGB::Green;
	FastLED.show();
}

void setRed() {
	// setColor(HIGH,LOW,LOW);
	leds[0] = CRGB::Red;
	FastLED.show();
}

void setYellow() {
	// setColor(LOW,LOW,HIGH);
	leds[0] = CRGB::Yellow;
	FastLED.show();
}

void setOff() {
	// setColor(LOW,LOW,LOW);
	leds[0] = CRGB::Black;
	FastLED.show();
}

void setWhite() {
	// setColor(HIGH,HIGH,HIGH);
	leds[0] = CRGB::White;
	FastLED.show();
}

void error() {
	setRed();
}


void openGate(){
	door.write(SERVO_OPEN);
	delay(100);
}

void closeGate(){
	door.write(SERVO_CLOSE);
	delay(100);
}

void loop() {
	rfid_tag_present_prev = rfid_tag_present;

	_rfid_error_counter += 1;
	if(_rfid_error_counter > 2){
	_tag_found = false;
	}

	byte bufferATQA[2];
	byte bufferSize = sizeof(bufferATQA);

	mfrc522.PCD_WriteRegister(mfrc522.TxModeReg, 0x00);
	mfrc522.PCD_WriteRegister(mfrc522.RxModeReg, 0x00);

	mfrc522.PCD_WriteRegister(mfrc522.ModWidthReg, 0x26);

	MFRC522::StatusCode result = mfrc522.PICC_RequestA(bufferATQA, &bufferSize);

	if(result == mfrc522.STATUS_OK){
		if ( ! mfrc522.PICC_ReadCardSerial()) {
			return;
		}
		_rfid_error_counter = 0;
		_tag_found = true;        
	}

	rfid_tag_present = _tag_found;

	// Viene letto
	if (rfid_tag_present && !rfid_tag_present_prev){
		on_detect();
	}

	//finché resta appoggiato non faccio nulla
	if (!rfid_tag_present && rfid_tag_present_prev){
		on_detach();
	}
}
