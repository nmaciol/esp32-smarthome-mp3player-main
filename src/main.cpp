/*  *
 * The MP3 player is based on the following examples:
 * https://github.com/pschatzmann/arduino-audio-tools/blob/main/examples/examples-audiokit/streams-sd_mp3-audiokit/streams-sd_mp3-audiokit.ino
 *              and
 * https://github.com/pschatzmann/arduino-audio-tools/blob/main/examples/examples-tts/streams-google-audiokit/streams-google-audiokit.ino
 *
 */
#include <WiFi.h>
#include <SD.h>
#include <AudioTools.h>
#include <AudioTools/Disk/AudioSourceSD.h>
#include <AudioTools/AudioLibs/AudioBoardStream.h>
#include <AudioTools/AudioCodecs/CodecMP3Helix.h>
#include <AudioTools/Communication/AudioHttp.h>
#include <SimpleFTPServer.h>


//  Fix SD card access issue in FTP server
//  ----------------------------------------
// !!!!! Overwrite value in  .pio\libdeps\esp32dev\SimpleFTPServer\FtpServerKey.h
/// Line 63 #define DEFAULT_STORAGE_TYPE_ESP32 					STORAGE_SD

#undef DEFAULT_STORAGE_TYPE_ESP32
#define DEFAULT_STORAGE_TYPE_ESP32 STORAGE_SD

#include <PubSubClient.h>

#include <QueueList.h>
#include <credentials.h>
#include <IniFile.h>
#include <Regexp.h>

/*

  0.2.0 Add Live Stream  code optimization

*/
#define FIRMWARE_VERSION "0.2.2"

int chipSelect = PIN_AUDIO_KIT_SD_CARD_CS;

const int errorLED = 22;

// helper.h
void getTtmFileName(String text, char *buf);
bool setGlobalVar();

// Declare variables for setting up WLAN, FTP, MQQTT, TTS, etc.
String S_HOST_NAME = HOST_NAME;
String S_AUDIO_VOLUME = AUDIO_VOLUME;
String S_START_SOUND = START_SOUND;

String S_WIFI_SSID = WIFI_SSID;
String S_WIFI_PASSWORD = WIFI_PASSWORD;

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
QueueList<String> queueOrder;

// WiFi + MQTT + FTP
WiFiClient net;
URLStream url;
PubSubClient mqttClient(net);
FtpServer ftpServer;
File file; // final output stream

AudioBoardStream i2s(AudioKitEs8388V1);
EncodedAudioStream decoder(&i2s, new MP3DecoderHelix()); // Decoding stream

StreamCopy copier; //  Audio Stream Input

File audioFile;

bool bStartup = false;
bool bLiveStreamPause = false;


String acceptMime = "audio/mp3";
String order;
Str query;
String currentLiveStream;
float PreviousVolume = 0;
String PreviousLang = TTS_LANG;
String TempVolume = "";

// tts setup Google Query
const char *tts(const char *text, const char *lang = "de-DE", const char *speed = "0.7")
{
  query = TTS_QRY_GOOGLE;
  query.replace("%1", lang);
  query.replace("%2", speed);
  Str encoded(text);
  encoded.urlEncode();
  query.replace("%3", encoded.c_str());
  return query.c_str();
}

// TTM-Worker
void TTM_Worker_Google(String sTts)
{

  char buf[240];
  // Serial.println("TTM " + sTts);
  getTtmFileName(sTts, buf);
  // show results
  // Serial.print ("Converted string: ");
  // Serial.println (buf);
  SD.begin(chipSelect);
  if (!SD.open("/tts-mp3/" + String(buf) + ".mp3", FILE_READ))
  {

    // Serial.print("File  --- ");
    // Serial.print(buf);
    // Serial.println(" --- does not exist");
    //  url
    const char *url_str = tts(sTts.c_str(), S_TTS_LANG.c_str(), S_TTS_SPEED.c_str());
    // Serial.println("URL Query -- " + String(url_str));
    //  generate mp3 with the help of google translate
    url.begin(url_str, "audio/mp3");
    decoder.begin();
    // copy file
    SD.begin(chipSelect);
    file = SD.open("/tts-mp3/" + String(buf) + ".mp3", FILE_WRITE);
    file.seek(0); // overwrite from beginning
    copier.begin(file, url);
    copier.copyAll();
    file.close();
  }
  // Serial.println("TTM Push Queue MP3  " + String(PreviousVolume));
  if (PreviousVolume > 0.01)
  {
    queueOrder.push("mp3" + TempVolume + "!/tts-mp3//" + String(buf) + ".mp3");
  }
  else
  {
    queueOrder.push("mp3/tts-mp3//" + String(buf) + ".mp3");
    // Serial.println ("TTM -> TTS " +  String(buf));
  }
}

// Button 3
void stopPlaySound(bool, int, void *)
{
  // Serial.println("Stop Play Sound");
  currentLiveStream = "";
  decoder.end();
  url.flush();
  url.end();
  url.clear();
  copier.end();
}
// Button 4
void playTestSound(bool, int, void *)
{
  // Serial.println("Sound started--Key");
  queueOrder.push("mp3" + S_START_SOUND);
}
// Button 5
void audioVolumeDown(bool, int, void *)

{
  i2s.incrementVolume(-0.05);
};
// Button 6
void audioVolumeUp(bool, int, void *)
{
  i2s.incrementVolume(0.05);
};

// MQTT Callback
void mqttCallback(char *topic, byte *message, unsigned int length)
{
  // Serial.print("Message arrived on topic: ");
  // Serial.print(topic);
  // Serial.print(". Message: ");
  String messageTemp;

  for (int i = 0; i < length; i++)
  {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }

  Serial.println();

  Serial.println(topic);

  // Execute MQTT-command
  if (String(topic) == S_HOST_NAME + "/mp3" or String(topic) == S_MQTT_HOUSE + "/mp3")
  {
    // Serial.print("MP3 starting  -- ");
    queueOrder.push("mp3" + messageTemp);
  }
  else if (String(topic) == S_HOST_NAME + "/incVol" or String(topic) == S_MQTT_HOUSE + "/incVol")
  {
    // Serial.println("Setting Volume");
    if (PreviousVolume > 0) {
      i2s.incrementVolume((float)messageTemp.toFloat());
    }
    else {
      i2s.incrementVolume((float)messageTemp.toFloat());
      PreviousVolume=i2s.getVolume();
    }
  }
  else if (String(topic) == S_HOST_NAME + "/setVol" or String(topic) == S_MQTT_HOUSE + "/setVol")
  {
    // Serial.println("Setting Volume");
    i2s.setVolume((float)messageTemp.toFloat());
    PreviousVolume = 0;
  }
  else if (String(topic) == S_HOST_NAME + "/resVol" or String(topic) == S_MQTT_HOUSE + "/resVol")
  {
    // Serial.println("Setting Volume");
    i2s.setVolume((float)S_AUDIO_VOLUME.toFloat());
    PreviousVolume = 0;
  }
  else if (String(topic) == S_HOST_NAME + "/stop" or String(topic) == S_MQTT_HOUSE + "/stop")
  {
    Serial.println("MQTT Stopping playing");
    currentLiveStream = "";
    i2s.setMute(true);
    decoder.end();
    url.flush();
    url.end();
    url.clear();
    // i2s.end();
    copier.end();
    delay(100);
    i2s.setMute(false);
    //queueOrder.push("mp3" + S_START_SOUND);
  }
  else if (String(topic) == S_HOST_NAME + "/tts" or String(topic) == S_MQTT_HOUSE + "/tts")
  {
    queueOrder.push("tts" + messageTemp);
  }
  else if (String(topic) == S_HOST_NAME + "/ls/mp3" or String(topic) == S_MQTT_HOUSE + "/ls/mp3")
  {
    Serial.println("ls/mp3");
    currentLiveStream = messageTemp;
    acceptMime = "audio/mp3";
  }
  else if (String(topic) == S_HOST_NAME + "/ls/aac" or String(topic) == S_MQTT_HOUSE + "/ls/aac")
  {
    Serial.println("ls/aac");
    currentLiveStream = messageTemp;
    acceptMime = "audio/aac";
  }
  else if (String(topic) == S_HOST_NAME + "/ttm" or String(topic) == S_MQTT_HOUSE + "/ttm")
  {
    // Serial.print("TTM starting  -- ");
    if (messageTemp.length() > S_TTS_MAX_LEN_TTM.toInt())
    {
      queueOrder.push("tts" + messageTemp);
    }
    else
    {
      queueOrder.push("ttm" + messageTemp);
    }
  }
  else if (String(topic) == S_HOST_NAME + "/delttm")
  {
    char buf[240];
    getTtmFileName(messageTemp, buf);
    // show results
    SD.begin(chipSelect);

    String filename = "/tts-mp3/" + String(buf) + ".mp3";
    SD.begin(chipSelect);
    if (SD.remove(filename))
    {
      // Serial.print("File ");
      // Serial.print(filename);
      // Serial.println(" deleted successfully.");
    }
    else
    {
      // Serial.print("Failed to delete file ");
      // Serial.println(filename);
    }
  }
  else if (String(topic) == S_HOST_NAME + "/speed" or String(topic) == S_MQTT_HOUSE + "/speed")
  {
    S_TTS_SPEED = messageTemp;
  }
  else if (String(topic) == S_HOST_NAME + "/ping")
  {
    uint32_t number = ESP.getFreeHeap();
    char buffer[12];
    sprintf(buffer, "%u", number);
    // Serial.println("Connection test Ping");
    mqttClient.publish((S_HOST_NAME + "/FreeHeap").c_str(), (String(buffer)).c_str(), 24);
    mqttClient.publish((S_HOST_NAME + "/version").c_str(), FIRMWARE_VERSION, 24);
  

    mqttClient.publish((S_HOST_NAME + "/currVol").c_str(), (String(i2s.getVolume())).c_str(), 24);
    mqttClient.publish((S_HOST_NAME + "/IP").c_str(),  (WiFi.localIP().toString()).c_str());

    // Sleep Mode
    wifi_ps_type_t ps;
    esp_wifi_get_ps(&ps);
    if (ps == WIFI_PS_MIN_MODEM)
    {
      mqttClient.publish((S_HOST_NAME + "/sleepMode").c_str(), "MIN_MODEM");
    }
    else if (ps == WIFI_PS_MAX_MODEM)
    {
      mqttClient.publish((S_HOST_NAME + "/sleepMode").c_str(), "MAX_MODEM");
    }
    else
    {
      mqttClient.publish((S_HOST_NAME + "/sleepMode").c_str(), "NONE");
    }

    // WiFi RSSI
    mqttClient.publish((S_HOST_NAME + "/rssi").c_str(), (String(WiFi.RSSI())).c_str());

  }
  else if (String(topic) == S_HOST_NAME + "/reboot")
  {
    // Serial.println("Reboot");
    i2s.end();
    decoder.end();
    ESP.restart();
  }
  else if (String(topic) == S_HOST_NAME + "/lpm")  // Low Power Mode
  {
    if(messageTemp=="on"){
      Serial.print("WiFi set sleep mode ON");
      delay(100);
      //WiFi.setSleep(true); 
      esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
      delay(100);
      //queueOrder.push("ttm20!Low Power Mode on");
    } else {
      WiFi.setSleep(false);
    }
    
  } 

}

// MQTT reconnect
void mqttReconnect()
{
  // Loop until we're reconnected
  while (!mqttClient.connected())
  {
    // Serial.print("Attempting MQTT connection...");
    if (mqttClient.connect((S_HOST_NAME + "--mp3Player").c_str(), S_MQTT_USER.c_str(), S_MQTT_PASSWORD.c_str()))
    {

      // Serial.println("connected");
      //  Host
      mqttClient.subscribe((S_HOST_NAME + "/mp3").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/reboot").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/setVol").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/incVol").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/resVol").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/stop").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/tts").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/ttm").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/delttm").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/ping").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/speed").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/ls/aac").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/ls/mp3").c_str());
      mqttClient.subscribe((S_HOST_NAME + "/lpm").c_str());  // Low Power Mode

      // House
      mqttClient.subscribe((S_MQTT_HOUSE + "/tts").c_str());
      mqttClient.subscribe((S_MQTT_HOUSE + "/ttm").c_str());
      mqttClient.subscribe((S_MQTT_HOUSE + "/setVol").c_str());
      mqttClient.subscribe((S_MQTT_HOUSE + "/incVol").c_str());
      mqttClient.subscribe((S_MQTT_HOUSE + "/resVol").c_str());
      mqttClient.subscribe((S_MQTT_HOUSE + "/stop").c_str());
      mqttClient.subscribe((S_MQTT_HOUSE + "/mp3").c_str());
      mqttClient.subscribe((S_MQTT_HOUSE + "/speed").c_str());
      mqttClient.subscribe((S_MQTT_HOUSE + "/ls/mp3").c_str());      
      mqttClient.subscribe((S_MQTT_HOUSE + "/ls/aac").c_str());
      // Startup
      delay(100);
      if (bStartup)
      {
        mqttClient.publish((S_MQTT_HOUSE + "/IP").c_str(), (S_HOST_NAME + " - " + WiFi.localIP().toString()).c_str());
        bStartup = false;
      }
    }
    else
    {
      // Serial.print("failed, rc=");
      // Serial.print(mqttClient.state());
      // Serial.println(" try again in 5 seconds");
      //  Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

// Setuo  i2s, Wifi, MQTT, FTP
void setup()
{
  
  Serial.begin(115200); // <--------------- do not comment out
  pinMode(errorLED, OUTPUT);

  //  For development
  //--------------------
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Error);
  // Statt Info geht; Debug, Info, Warning, Error

  // setup audiokit before SD!
  // setup output
  auto cfg = i2s.defaultConfig(TX_MODE);
  if (!i2s.begin(cfg))
  {
    // Serial.println("i2s failed");
    stop();
  }

  // Overwrite globVar with AppIni values

  if (!setGlobalVar())
  {
    // SD Card error
    digitalWrite(errorLED, LOW);
  }
  else
  {
    digitalWrite(errorLED, HIGH);
  }

  i2s.setVolume(S_AUDIO_VOLUME.toFloat());
  // setup additional buttons
  i2s.addDefaultActions();
  i2s.addAction(i2s.getKey(3), stopPlaySound);
  i2s.addAction(i2s.getKey(4), playTestSound);
  i2s.addAction(i2s.getKey(5), audioVolumeDown);
  i2s.addAction(i2s.getKey(6), audioVolumeUp);

  // Connecting to a WiFi network
  // Serial.println();
  // Serial.print("Connecting to SSID: ");
  // Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.begin(S_WIFI_SSID.c_str(), S_WIFI_PASSWORD.c_str());

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    // Serial.print(".");
  }

  // Serial.println("WiFi connected");
  // Serial.println("IP address: ");
  // Serial.println(WiFi.localIP());

  // Setup MQTT
  mqttClient.setServer(S_MQTT_SERVER.c_str(), S_MQTT_PORT.toInt());
  mqttClient.setCallback(mqttCallback);

  delay(10);

  // Start FTP server with username and password
  ftpServer.begin(S_FTP_SVR_USER.c_str(), S_FTP_SVR_PASSWORD.c_str());

  // Serial.println("FTP server started!");
  //
  delay(10);
  queueOrder.push("mp3" + S_START_SOUND);
  bStartup = true;

  Serial.println("+-------------------------------------------+");

  Serial.println(" Host-Name  : " + S_HOST_NAME);
  Serial.println(" MQTT House : " + S_MQTT_HOUSE);
  Serial.println(" WiFi SSID  : " + S_WIFI_SSID);
  Serial.println(" FTP  user  : " + S_FTP_SVR_USER);
  Serial.println(" MQTT SRV   : " + S_MQTT_SERVER);
  Serial.println(" Volume     : " + String(i2s.getVolume()));
  Serial.println(" ");
  Serial.println(" IP         : " + WiFi.localIP().toString());
  Serial.println(" RSSI       : " + String(WiFi.RSSI()) + " dBm");
  Serial.println(" ");
}

void loop()
{


  // MQTT Reconnect when the connection is lost
  if (!mqttClient.connected())
  {
    mqttReconnect();
  }

  // MQTT
  mqttClient.loop();
  mqttClient.loop();

  // Execute all actions if the corresponding button/ pin is low
  i2s.processActions();

  // Handle FTP server operations
  ftpServer.handleFTP(); // Continuously process FTP requests

  // Stop Live Strem
  if (!queueOrder.isEmpty() and currentLiveStream.length() > 0 and !bLiveStreamPause)
  {
    // Serial.println("Stop LS");
    decoder.end();
    delay(100);
    url.flush();
    url.end();
    url.clear();
    copier.end();
    bLiveStreamPause = true;
  }

  // Check out queues and work items
  if (!copier.copy()  or copier.copy() == 0)  // Is free to take new items
    {
      if ( true){
      // Start Live Stream
        if (currentLiveStream.length() > 0 and queueOrder.isEmpty())
        {
          // Serial.println("Start LS");
          //  Prefix Volume control
          if (currentLiveStream.substring(2, 3).compareTo("!") == 0)
          {
            if (currentLiveStream.substring(0, 2).toInt() > 3)
            {
              if (PreviousVolume == 0)
              {
                PreviousVolume = i2s.getVolume();
              }
              TempVolume = currentLiveStream.substring(0, 2);
              i2s.setVolume(("0." + TempVolume).toFloat());
              // Serial.println("New volume -- " + String(i2s.getVolume()));
            }
          }
          // LV-Stream
          Serial.println("LS -- " + currentLiveStream );
          decoder.begin();
          if (!url.begin(currentLiveStream.c_str(), (acceptMime.c_str()))){
        
            Serial.println("LS  -- error " );
          
            currentLiveStream = "";
            mqttClient.publish((S_MQTT_HOUSE + "/error").c_str(), (S_HOST_NAME + " - Live stream address no response").c_str());
          } else {
            Serial.println("LS  -- start " );
            copier.begin(decoder, url);
            copier.copy();
            bLiveStreamPause = false;
          }
        }
      }

    // MP3 TTS TTM
    if (!copier.copy())
    {

      if (PreviousVolume > 0)
      {
        i2s.setVolume(PreviousVolume);
        PreviousVolume = 0;
        TempVolume = "";
      }
      if (PreviousLang.length() > 0)
      {
        S_TTS_LANG = PreviousLang;
        PreviousLang = "";
      }

      if (!queueOrder.isEmpty())
      {
        String order = queueOrder.pop();
        // Serial.println ("Loop - " + order);
        String orderTyp = order.substring(0, 3);
        String orderText = order.substring(3);
        // Prefix Volume control
        if (orderText.substring(2, 3).compareTo("!") == 0)
        {
          if (orderText.substring(0, 2).toInt() > 3)
          {
            if (PreviousVolume == 0)
            {
              PreviousVolume = i2s.getVolume();
            }
            TempVolume = orderText.substring(0, 2);
            i2s.setVolume(("0." + TempVolume).toFloat());
            // Serial.println("New volume -- " + String(i2s.getVolume()));
          }
          else if (orderText.substring(0, 2) == "en" or orderText.substring(0, 2) == "pl")
          {
            PreviousLang = S_TTS_LANG;
            S_TTS_LANG = orderText.substring(0, 2);
          }
          // Serial.println("Prefix -- " + orderText.substring(0, 2));
          orderText = orderText.substring(3);
        }

        if (orderTyp == "mp3")
        {
          // Serial.println("Loop Sound started -- " + orderText);
          SD.begin(chipSelect);
          audioFile = SD.open(orderText);
          if (audioFile)
          {
            decoder.begin();
            copier.begin(decoder, audioFile);
            copier.copy();
          }
          else
          {
            mqttClient.publish((S_MQTT_HOUSE + "/error").c_str(), (S_HOST_NAME + " / " + orderText + " - does not exist").c_str());
            // Serial.println("File open failed -- " + orderText );
          }
        }
        else if (orderTyp == "ttm")
        {

          TTM_Worker_Google(orderText);
        }
        else if (orderTyp == "tts")
        {
          // Serial.println("TTS " + orderText);
          const char *url_str = tts(orderText.c_str(), S_TTS_LANG.c_str(), S_TTS_SPEED.c_str());
          // generate mp3 with the help of google tts
          decoder.begin();
          if (!url.begin(url_str, "audio/mp3"))
          {
            mqttClient.publish((S_MQTT_HOUSE + "/error").c_str(), (S_HOST_NAME + " / " + url_str + " - no response").c_str());
          }
          else
          {
            // Serial.println("TTS Url: " + String(url_str));
            copier.begin(decoder, url);
            copier.copyAll();
          }
        }
      }
    }
  }
}
