/*  * 
 * The MP3 player is based on the following examples:
 * https://github.com/pschatzmann/arduino-audio-tools/blob/main/examples/examples-audiokit/streams-sd_mp3-audiokit/streams-sd_mp3-audiokit.ino
 *              and
 * https://github.com/pschatzmann/arduino-audio-tools/blob/main/examples/examples-tts/streams-google-audiokit/streams-google-audiokit.ino
 * 
 */
 
#include <SD.h>
#include "AudioTools.h"
#include "AudioTools/Disk/AudioSourceSD.h"
#include "AudioTools/AudioLibs/AudioBoardStream.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "AudioTools/Communication/AudioHttp.h"
#include <WiFi.h>
#include <SimpleFTPServer.h>

#undef DEFAULT_STORAGE_TYPE_ESP32
#define DEFAULT_STORAGE_TYPE_ESP32 STORAGE_SD

#include <PubSubClient.h>

#include <QueueList.h>
#include <credentials.h>
#include <IniFile.h>
#include <Regexp.h>

#define FIRMWARE_VERSION "0.1.0"

int chipSelect=PIN_AUDIO_KIT_SD_CARD_CS;

// !!!!! Overwrite value in  .pio\libdeps\esp32dev\SimpleFTPServer\FtpServerKey.h
/// Line 63 #define DEFAULT_STORAGE_TYPE_ESP32 					STORAGE_SD
	


// helper.h
void getTtmFileName(String text,  char* buf);
void setGlobalVar();

// Declare variables for setting up WLAN, FTP, MQQTT, TTS, etc. 
String S_HOST_NAME = HOST_NAME;
String S_AUDIO_VOLUME = AUDIO_VOLUME;
String S_START_SOUND = START_SOUND;

String S_WIFI_SSID = WIFI_SSID ;
String S_WIFI_PASSWORD = WIFI_PASSWORD ;

String S_FTP_SVR_USER = FTP_SVR_USER;
String S_FTP_SVR_PASSWORD = FTP_SVR_PASSWORD;

String S_MQTT_SERVER = MQTT_SERVER;
String S_MQTT_PORT = MQTT_PORT;
String S_MQTT_USER = MQTT_USER;
String S_MQTT_PASSWORD = MQTT_PASSWORD;
String S_MQTT_HOUSE = MQTT_HOUSE;

String S_TTS_QRY_GOOGLE = TTS_QRY_GOOGLE;
String S_TTS_LANG = TTS_LANG;
String S_TTS_SPEED = TTS_SPEED;
String S_TTS_MAX_LEN_TTM = TTS_MAX_LEN_TTM;

// create a queue of strings messages.
QueueList <String> queueOrder;

// WiFi + MQTT + FTP
WiFiClient net;
URLStream url;  
PubSubClient mqttClient(net);
FtpServer   ftpServer;  
File file; // final output stream


AudioBoardStream i2s(AudioKitEs8388V1);
EncodedAudioStream decoder(&i2s, new MP3DecoderHelix()); // Decoding stream

StreamCopy copierMP3;    // MP§ Player
StreamCopy copierTTM; //  Text to MP3 File Onliene -> Offline
StreamCopy copierTTS; //  Text to speech

File audioFile;

bool bStartup = false;
bool bCopierActive = false;

String order;
Str query;
float PreviousVolume = 0;
String PreviousLang = TTS_LANG;
String TempVolume = "";

// tts setup Google Query
const char* tts(const char* text, const char* lang="de-DE", const char* speed="0.7"){  
  query  = TTS_QRY_GOOGLE;
  query.replace("%1",lang);
  query.replace("%2",speed);
  Str encoded(text);
  encoded.urlEncode();
  query.replace("%3", encoded.c_str());
  return query.c_str();
}

// TTM-Worker
void  TTM_Worker_Google(String sTts){

    char buf [240];
    Serial.println("TTM " + sTts);
    getTtmFileName(sTts,  buf);
    // show results
    Serial.print ("Converted string: ");
    Serial.println (buf);
    SD.begin(chipSelect);
    if (!SD.open("/tts-mp3/" + String(buf) + ".mp3", FILE_READ)) {

      Serial.print("File  --- ");
      Serial.print(buf);
      Serial.println(" --- does not exist");
      // url 
      const char* url_str = tts(sTts.c_str(), S_TTS_LANG.c_str(), S_TTS_SPEED.c_str());
      Serial.println("URL Query -- " + String(url_str));
      // generate mp3 with the help of google translate
      url.begin(url_str ,"audio/mp3");
      decoder.begin();
        // copy file
      SD.begin(chipSelect);
      file = SD.open("/tts-mp3/" + String(buf) + ".mp3", FILE_WRITE);
      file.seek(0); // overwrite from beginning
      copierTTM.begin(file, url);
      copierTTM.copyAll();
      //copierTTM.end();
      file.close();
      url.clear();
      url.end();
    } 
    Serial.println("TTM Push Queue MP3  " + String(PreviousVolume));
    if(PreviousVolume > 0.01){
      queueOrder.push("mp3" +  TempVolume + "!/tts-mp3//" + String(buf) + ".mp3");
    } else {
      queueOrder.push("mp3/tts-mp3//" +  String(buf) + ".mp3");

    }
}

// Button 3
void stopPlaySound(bool, int, void *)
{
  Serial.println("Stop Play Sound");
  copierMP3.end();
  decoder.end();
}
// Button 4
void playTestSound(bool, int, void *)
{
  Serial.println("Sound started--Key");
  queueOrder.push("mp3" + S_START_SOUND);  
}
// Button 5
void audioVolumeDown (bool, int, void *)
{   
  i2s.incrementVolume(-0.05);
};
// Button 6
void audioVolumeUp (bool, int, void *)
{ 
  i2s.incrementVolume(0.05);
};


//MQTT Callback
void mqttCallback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String messageTemp;

  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();

  // Execute MQTT-command
  if (String(topic) == S_HOST_NAME + "/mp3" or  String(topic) ==  S_MQTT_HOUSE + "/mp3") {
      Serial.print("MP3 starting  -- ");
      queueOrder.push("mp3" + messageTemp);        
  } else if (String(topic) ==  S_HOST_NAME + "/volume" or  String(topic) ==  S_MQTT_HOUSE + "/volume") {
      Serial.println("Setting Volume");
      i2s.setVolume((float)messageTemp.toFloat());
  } else if (String(topic) ==  S_HOST_NAME + "/stop" or  String(topic) ==  S_MQTT_HOUSE + "/stop") {
      Serial.println("Stopping playing");
      copierMP3.end();
      decoder.end();
      //ESP.restart();
  } else if (String(topic) ==  S_HOST_NAME + "/tts" or  String(topic) == S_MQTT_HOUSE + "/tts") {
      queueOrder.push("tts" + messageTemp);
  } else if (String(topic) ==  S_HOST_NAME + "/ttm" or  String(topic) == S_MQTT_HOUSE + "/ttm") {
      Serial.print("TTM starting  -- ");
      if(messageTemp.length() > S_TTS_MAX_LEN_TTM.toInt()){
          queueOrder.push("tts" +messageTemp);
      } else {
          queueOrder.push("ttm" +messageTemp);
      }
  } else if (String(topic) ==  S_HOST_NAME + "/delttm" ) {
        char buf [240];        
        getTtmFileName(messageTemp, buf);
        // show results
        SD.begin(chipSelect);
            
        String filename = "/tts-mp3/" + String(buf) + ".mp3";
        SD.begin(chipSelect);
        if (SD.remove(filename)) {
          Serial.print("File ");
          Serial.print(filename);
          Serial.println(" deleted successfully.");
        } else {
          Serial.print("Failed to delete file ");
          Serial.println(filename);
        }
  } else if (String(topic) ==  S_HOST_NAME + "/speed" or  String(topic) == S_MQTT_HOUSE + "/speed") {
      S_TTS_SPEED =  messageTemp;
  } else if (String(topic) ==  S_HOST_NAME + "/ping" ) {
      uint32_t number =  ESP.getFreeHeap();
      char buffer[12];
      sprintf(buffer, "%u" , number);
      Serial.println("Connection test Ping");  
      mqttClient.publish((S_HOST_NAME + "/FreeHeap").c_str(), ( String(buffer)).c_str(),24);
      mqttClient.publish((S_HOST_NAME + "/version").c_str(), FIRMWARE_VERSION,24);
  } else if (String(topic) == S_HOST_NAME + "/reboot") {
      Serial.println("Reboot");
      i2s.end();
      decoder.end();
      ESP.restart();
  } else {
      Serial.println("Not playing");
  }
}

// MQTT reconnect
void mqttReconnect() {
    // Loop until we're reconnected
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (mqttClient.connect((S_HOST_NAME + "--mp3Player").c_str(), S_MQTT_USER.c_str(), S_MQTT_PASSWORD.c_str())) {
        
        Serial.println("connected");
        // Host
        mqttClient.subscribe((S_HOST_NAME + "/mp3").c_str());
        mqttClient.subscribe((S_HOST_NAME + "/reboot").c_str());
        mqttClient.subscribe((S_HOST_NAME + "/volume").c_str());
        mqttClient.subscribe((S_HOST_NAME + "/stop").c_str());
        mqttClient.subscribe((S_HOST_NAME + "/tts").c_str());
        mqttClient.subscribe((S_HOST_NAME + "/ttm").c_str());
        mqttClient.subscribe((S_HOST_NAME + "/delttm").c_str());
        mqttClient.subscribe((S_HOST_NAME + "/ping").c_str());
        mqttClient.subscribe((S_HOST_NAME + "/speed").c_str());

        // House
        mqttClient.subscribe((S_MQTT_HOUSE + "/tts").c_str());
        mqttClient.subscribe((S_MQTT_HOUSE + "/ttm").c_str());
        mqttClient.subscribe((S_MQTT_HOUSE + "/volume").c_str());
        mqttClient.subscribe((S_MQTT_HOUSE + "/stop").c_str());
        mqttClient.subscribe((S_MQTT_HOUSE + "/mp3").c_str());
        mqttClient.subscribe((S_MQTT_HOUSE + "/speed").c_str());
        // Startup
        delay (100);
        if(bStartup){
          mqttClient.publish((S_HOST_NAME + "/IP").c_str(),(S_HOST_NAME + " - " + WiFi.localIP().toString()).c_str());
          bStartup=false;
        }
    } else {
        Serial.print("failed, rc=");
        Serial.print(mqttClient.state());
        Serial.println(" try again in 5 seconds");
        // Wait 5 seconds before retrying
        delay(5000);
     }
  }
}

// Setuo  i2s, Wifi, MQTT, FTP
void setup(){
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Info);  

  // setup audiokit before SD!
  // setup output
  auto cfg = i2s.defaultConfig(TX_MODE);
  if (!i2s.begin(cfg)){
    Serial.println("i2s failed");
    stop();
  }

  // Overwrite globVar with AppIni values
  setGlobalVar();
  i2s.setVolume(S_AUDIO_VOLUME.toFloat());
  // setup additional buttons
  i2s.addDefaultActions();
  i2s.addAction(i2s.getKey(3), stopPlaySound);
  i2s.addAction(i2s.getKey(4), playTestSound);
  i2s.addAction(i2s.getKey(5), audioVolumeDown);
  i2s.addAction(i2s.getKey(6), audioVolumeUp);

  // Connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to SSID: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.begin(S_WIFI_SSID, S_WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  // Setup MQTT
  mqttClient.setServer(S_MQTT_SERVER.c_str(), S_MQTT_PORT.toInt());
  mqttClient.setCallback(mqttCallback);

  delay(10);

  // Start FTP server with username and password
  ftpServer.begin(S_FTP_SVR_USER.c_str(), S_FTP_SVR_PASSWORD.c_str()); 
  Serial.println("FTP server started!");
  //
  delay(10);
  queueOrder.push("mp3" + S_START_SOUND);
  bStartup= true;
}


void loop(){

  if ( !copierMP3.copy() and !copierTTS.copy() and !copierTTM.copy())  {
    if(PreviousVolume > 0){
      i2s.setVolume(PreviousVolume);
      PreviousVolume =0;
      TempVolume="";
    }
    if(PreviousLang.length() > 0){
      S_TTS_LANG = PreviousLang;
      PreviousLang = "";
    }
    bCopierActive = false;
  } else {
    bCopierActive = true;
  }

  // MQTT Reconnect when the connection is lost
  if (!mqttClient.connected()) {
    mqttReconnect();
  }

  //MQTT
  mqttClient.loop();

  // Execute all actions if the corresponding button/ pin is low
  i2s.processActions();

  // Handle FTP server operations
  ftpServer.handleFTP(); // Continuously process FTP requests

  // Check out queues and work items
  if (!bCopierActive  )  {

    if(!queueOrder.isEmpty()){
        bCopierActive = true;
        String order = queueOrder.pop();
        String orderTyp = order.substring(0,3);
        String orderText = order.substring(3);

        if( orderText.substring(2,3 ).compareTo("!") == 0 )
        {
          if(orderText.substring(0, 2).toInt() > 3) {
            PreviousVolume= i2s.getVolume();
            TempVolume=orderText.substring(0, 2);
            i2s.setVolume(("0." + TempVolume).toFloat());
            Serial.println("New volume -- " + String(i2s.getVolume()));
          }  else if(orderText.substring(0,2) == "en" or orderText.substring(0,2) == "pl") {
            PreviousLang = S_TTS_LANG;
            S_TTS_LANG =  orderText.substring(0,2);
          }
          Serial.println("Prefix -- " + orderText.substring(0, 2));
          orderText =  orderText.substring(3);
        }

        if(orderTyp == "mp3"){
          Serial.println("Loop Sound started -- " + orderText);
          SD.begin(chipSelect);
          audioFile = SD.open(orderText);
          if(audioFile){
            decoder.begin();
            copierMP3.begin(decoder, audioFile);
            copierMP3.copy();
          } else {
            Serial.println("File open failed -- " + orderText );
          }
        } else if (orderTyp == "ttm") {

          TTM_Worker_Google( orderText);

        } else if (orderTyp == "tts") {
          Serial.println("TTS " + orderText);
          const char* url_str = tts(orderText.c_str(), S_TTS_LANG.c_str(), S_TTS_SPEED.c_str());
          // generate mp3 with the help of google tts
          decoder.begin();
          url.begin(url_str ,"audio/mp3");
          Serial.println("TTS Url: " + String(url_str));
          copierTTS.begin(decoder, url);
          copierTTS.copyAll();
          url.clear();
          url.end();
        }
    }
  }
}
