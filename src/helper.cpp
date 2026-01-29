#ifndef HELPER_H
#define HELPER_H



#include <Arduino.h>
#include <cstdint>
#include <IniFile.h>
#include <Regexp.h>
#include <credentials.h>
#include <SD.h>

void printAppIniErrorMessage(uint8_t e);

extern  int chipSelect;


extern String S_HOST_NAME;
extern String S_AUDIO_VOLUME;
extern String S_START_SOUND;

extern String S_WIFI_SSID ;
extern String S_WIFI_PASSWORD ;

extern String S_FTP_SVR_USER;
extern String S_FTP_SVR_PASSWORD;

extern String S_MQTT_SERVER;
extern String S_MQTT_PORT;
extern String S_MQTT_USER;
extern String S_MQTT_PASSWORD;
extern String S_MQTT_HOUSE;

extern String S_TTS_QRY_GOOGLE;
extern String S_TTS_LANG;
extern String S_TTS_SPEED;
extern String S_TTS_MAX_LEN_TTM;

// Overwrite globVar with AppIni values
bool setGlobalVar(){
  
    Serial.println(" ------  Start set global params ------------");
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
      return(false);
    
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
        return(false);
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
        if (ini.getValue("mqtt", "house", buffer, bufferLen)) {
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
    return(true);
  }



// app.ini Error rutine
void printAppIniErrorMessage(uint8_t e)
  {
    switch (e) {
    case IniFile::errorNoError:
      Serial.print("no error");
      break;
    case IniFile::errorFileNotFound:
      Serial.print("file not found");
      break;
    case IniFile::errorFileNotOpen:
      Serial.print("file not open");
      break;
    case IniFile::errorBufferTooSmall:
      Serial.print("buffer too small");
      break;
    case IniFile::errorSeekError:
      Serial.print("seek error");
      break;
    case IniFile::errorSectionNotFound:
      Serial.print("section not found");
      break;
    case IniFile::errorKeyNotFound:
      Serial.print("key not found");
      break;
    case IniFile::errorEndOfFile:
      Serial.print("end of file");
      break;
    case IniFile::errorUnknownError:
      Serial.print("unknown error");
      break;
    default:
      Serial.print("unknown error value");
      break;
    }
   
}


void getTtmFileName(String text,  char* buf) {

  MatchState ms (buf);
  //char buf [240]; 

  strcpy (buf, text.c_str());
  ms.Target (buf);    // recompute length
  // replace vowels with *
  ms.GlobalReplace ("Heizungsraum", "Hzg");    
  ms.GlobalReplace ("Minuten", "M");    
  ms.GlobalReplace ("Waschmaschine", "Wm");    
  ms.GlobalReplace ("[aeiouöüßä .,?:]", "");     
  ms.GlobalReplace ("mm", "m");     
  ms.GlobalReplace ("nn", "n");     
  ms.GlobalReplace ("rr", "r");     
  ms.GlobalReplace ("ff", "f");      
  ms.GlobalReplace ("ll", "l");      

  // show results
  Serial.print ("Converted string: ");
  Serial.println (buf);
}


#endif // HELPER_H