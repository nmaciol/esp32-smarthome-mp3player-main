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
#include <PubSubClient.h>
#include <QueueList.h>
#include <credentials.h>
#include <IniFile.h>
#include <Regexp.h>

const int chipSelect=PIN_AUDIO_KIT_SD_CARD_CS;

// helper.h
void printAppIniErrorMessage(uint8_t e, bool eol = true);

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
QueueList <String> queueMp3;
QueueList <String> queueTTM;
QueueList <String> queueTTS;

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

bool bActiveMP3 = false;
bool bActiveTTM = false;
bool bActiveTTS = false;
bool bStartup = false;

String smp3File;
Str query;


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
void  TTM_Worker(){

  char buf [240];
  String sTts;
  MatchState ms (buf);
  if (!queueTTM.isEmpty() )
  {
    sTts = queueTTM.pop().substring(0, 239);
    Serial.println("TTM " + sTts);
    strcpy (buf, sTts.c_str());
    ms.Target (buf);    // recompute length
    // replace vowels with *
    ms.GlobalReplace ("Heizungsraum", "Hzg");    
    ms.GlobalReplace ("Minuten", "M");    
    ms.GlobalReplace ("Waschmaschine", "Wm");    
    ms.GlobalReplace ("[aeiouöüßä .,?!:]", "");     
    ms.GlobalReplace ("mm", "m");     
    ms.GlobalReplace ("nn", "n");     
    ms.GlobalReplace ("rr", "r");     
    ms.GlobalReplace ("ff", "f");      
    ms.GlobalReplace ("ll", "l");      

    // show results
    Serial.print ("Converted string: ");
    Serial.println (buf);
    SD.begin(chipSelect);
    if (!SD.open("/tts-mp3/" + String(buf) + ".mp3", FILE_READ)) {

      Serial.print("File  --- ");
      Serial.print(buf);
      Serial.println(" --- does not exist");
      // url
      const char* url_str = tts(sTts.c_str());
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

    delay(500);
    Serial.println("TTM Push Queue MP3");
    queueMp3.push("/tts-mp3//" + String(buf) + ".mp3");
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
  queueMp3.push(S_START_SOUND);  
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
      queueMp3.push(messageTemp);        
  } else if (String(topic) ==  S_HOST_NAME + "/volume" or  String(topic) ==  S_MQTT_HOUSE + "/volume") {
      Serial.println("Setting Volume");
      i2s.setVolume((float)messageTemp.toFloat());
  } else if (String(topic) ==  S_HOST_NAME + "/stop" or  String(topic) ==  S_MQTT_HOUSE + "/stop") {
      Serial.println("Stopping playing");
      copierMP3.end();
      decoder.end();
      //ESP.restart();
  } else if (String(topic) ==  S_HOST_NAME + "/tts" or  String(topic) == S_MQTT_HOUSE + "/tts") {
      queueTTS.push(messageTemp);
  } else if (String(topic) ==  S_HOST_NAME + "/ttm" or  String(topic) == S_MQTT_HOUSE + "/ttm") {
      Serial.print("TTM starting  -- ");
      if(messageTemp.length() > S_TTS_MAX_LEN_TTM.toInt()){
           queueTTS.push(messageTemp);
        } else {
            queueTTM.push(messageTemp);
        }
  } else if (String(topic) ==  S_HOST_NAME + "/speed" or  String(topic) == S_MQTT_HOUSE + "/speed") {
      S_TTS_SPEED =  messageTemp;
  } else if (String(topic) ==  S_HOST_NAME + "/ping" ) {
      uint32_t number =  ESP.getFreeHeap();
      char buffer[12];
      sprintf(buffer, "%u" , number);
      Serial.println("Connection test Ping");  
      mqttClient.publish((S_HOST_NAME + "/FreeHeep").c_str(), buffer,12);
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
        mqttClient.subscribe((S_HOST_NAME + "/mp3").c_str());
        mqttClient.subscribe((S_HOST_NAME + "/reboot").c_str());
        mqttClient.subscribe((S_HOST_NAME + "/volume").c_str());
        mqttClient.subscribe((S_HOST_NAME + "/stop").c_str());
        mqttClient.subscribe((S_HOST_NAME + "/tts").c_str());
        mqttClient.subscribe((S_HOST_NAME + "/ttm").c_str());
        mqttClient.subscribe((S_HOST_NAME + "/ping").c_str());
        mqttClient.subscribe((S_HOST_NAME + "/speed").c_str());
        mqttClient.subscribe((S_MQTT_HOUSE + "/tts").c_str());
        mqttClient.subscribe((S_MQTT_HOUSE + "/ttm").c_str());
        mqttClient.subscribe((S_MQTT_HOUSE + "/volume").c_str());
        mqttClient.subscribe((S_MQTT_HOUSE + "/stop").c_str());
        mqttClient.subscribe((S_MQTT_HOUSE + "/mp3").c_str());
        mqttClient.subscribe((S_MQTT_HOUSE + "/speed").c_str());

        delay (100);
        if(bStartup){
          mqttClient.publish((S_HOST_NAME + "/IP").c_str(),WiFi.localIP().toString().c_str());
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

  // --------------  app.ini ------------------
  SD.begin(chipSelect);
  // App.ini
  const size_t bufferLen = 180;
  char buffer[bufferLen];
  const char *filename = "/app.ini";

  IniFile ini(filename);
  if (!ini.open()) {
    Serial.print("ERROR - Ini file ");
    Serial.print(filename);
    Serial.println(" does not exist");
  
  } else {
    Serial.print("SUCCESS - Found ");
    Serial.print(filename);
    Serial.println(" file.");

    // Check the file is valid. This can be used to warn if any lines
    // are longer than the buffer.
    if (!ini.validate(buffer, bufferLen)) {
      Serial.print("ini file ");
      Serial.print(ini.getFilename());
      Serial.print(" not valid: ");
      printAppIniErrorMessage(ini.getError());
    } else {
    
      if (ini.getValue("main", "host-name", buffer, bufferLen)) {
        Serial.print("section 'main' has an entry 'host' with value ");
        Serial.println(buffer);
        S_HOST_NAME = buffer;
      }

      if (ini.getValue("main", "audio-volume", buffer, bufferLen)) {
        Serial.print("section 'main' has an entry 'audio-volume' with value ");
        Serial.println(buffer);
        S_AUDIO_VOLUME = buffer;
      }

      if (ini.getValue("main", "start-sound", buffer, bufferLen)) {
        Serial.print("section 'main' has an entry 'start-sound' with value ");
        Serial.println(buffer);
        S_START_SOUND = buffer;
      }

      if (ini.getValue("wifi", "ssid", buffer, bufferLen)) {
        Serial.print("section 'wifi' has an entry 'ssid' with value ");
        Serial.println(buffer);
        S_WIFI_SSID = buffer;
      }

      if (ini.getValue("wifi", "password", buffer, bufferLen)) {
        Serial.print("section 'wifi' has an entry 'password' with value ");
        Serial.println(buffer);
        S_WIFI_PASSWORD = buffer;
      }


      if (ini.getValue("mqtt", "server", buffer, bufferLen)) {
        Serial.print("section 'mqtt' has an entry 'Server' with value ");
        Serial.println(buffer);
        S_MQTT_SERVER = buffer;
      }
      if (ini.getValue("mqtt", "port", buffer, bufferLen)) {
        Serial.print("section 'mqtt' has an entry 'Port' with value ");
        Serial.println(buffer);
        S_MQTT_PORT = buffer;
      }
      if (ini.getValue("mqtt", "user", buffer, bufferLen)) {
        Serial.print("section 'mqtt' has an entry 'user' with value ");
        Serial.println(buffer);
        S_MQTT_USER = buffer;
      }

      if (ini.getValue("mqtt", "password", buffer, bufferLen)) {
        Serial.print("section 'mqtt' has an entry 'password' with value ");
        Serial.println(buffer);
        S_MQTT_PASSWORD = buffer;
      }
      if (ini.getValue("mqtt", "mqtt-house", buffer, bufferLen)) {
        Serial.print("section 'mqtt' has an entry 'House' with value ");
        Serial.println(buffer);
        S_MQTT_HOUSE = buffer;
      }

      if (ini.getValue("ftp", "user", buffer, bufferLen)) {
        Serial.print("section 'ftp' has an entry 'user' with value ");
        Serial.println(buffer);
        S_FTP_SVR_USER = buffer;
      }

      if (ini.getValue("ftp", "password", buffer, bufferLen)) {
        Serial.print("section 'ftp' has an entry 'password' with value ");
        Serial.println(buffer);
        S_FTP_SVR_PASSWORD = buffer;
      }

      if (ini.getValue("tts", "qry-google", buffer, bufferLen)) {
        Serial.print("section 'tts' has an entry 'Qry Google' with value ");
        Serial.println(buffer);
        S_TTS_QRY_GOOGLE = buffer;
      }

      if (ini.getValue("tts", "lang", buffer, bufferLen)) {
        Serial.print("section 'tts' has an entry 'Lang' with value ");
        Serial.println(buffer);
        S_TTS_LANG = buffer;
      }

      if (ini.getValue("tts", "spped", buffer, bufferLen)) {
        Serial.print("section 'tts' has an entry 'Speed' with value ");
        Serial.println(buffer);
        S_TTS_SPEED = buffer;
      }
      if (ini.getValue("tts", "max-len-ttm", buffer, bufferLen)) {
        Serial.print("section 'tts' has an entry 'Max-len-TTM' with value ");
        Serial.println(buffer);
        S_TTS_MAX_LEN_TTM = buffer;
      }

    }
  }


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

  //  --- 
  delay(10);
  queueMp3.push(S_START_SOUND);
  bStartup= true;
}


void loop(){

  // MP3 Player
  if (!copierMP3.copy()) {
    bActiveMP3 = false;
  } else {
    bActiveMP3 = true;
  }
 
  // Text to MP3
  if (!copierTTM.copy()) {
     bActiveTTM = false;
  } else {
     bActiveTTM = true;
  }

  // Text to Speech
  if (!copierTTS.copy()) {
    bActiveTTS = false;
   
  } else {
    bActiveTTS = true;
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
  if (!bActiveTTS and !bActiveTTM and !bActiveMP3  )  {

    if(!queueMp3.isEmpty()){
        //MP3
        bActiveMP3 = true;
        smp3File = queueMp3.pop();
        Serial.println("Loop Sound started -- " + smp3File);
        SD.begin(chipSelect);
        audioFile = SD.open(smp3File);
        if(audioFile){
          decoder.begin();
          copierMP3.begin(decoder, audioFile);
          copierMP3.copy();
        } else {
          Serial.println("File open failed -- " + smp3File );
        }
        
    } else if (!queueTTM.isEmpty()) {
        //TTM
        bActiveTTM = true;
        TTM_Worker();
      
    } else if (!queueTTS.isEmpty()) {
        //TTS
        bActiveTTS = true;
        String sTts;
        sTts = queueTTS.pop().substring(0, 239);
        Serial.println("TTS " + sTts);
        const char* url_str = tts(sTts.c_str());

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
