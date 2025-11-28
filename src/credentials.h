
#ifndef CREDENTIALS_H
#define CREDENTIALS_H


// All these settings are overwritten with values from the app.ini file. 
// If app.ini is unreadable or corrupt, these settings are used
#define HOST_NAME "gong-a"
#define AUDIO_VOLUME "0.6"
#define START_SOUND "/Doorbell.mp3"

#define WIFI_SSID "***"
#define WIFI_PASSWORD "***"
#define FTP_SVR_USER "***"
#define FTP_SVR_PASSWORD "***"

#define MQTT_SERVER "192.168.x.xxx"
#define MQTT_PORT "188"
#define MQTT_USER "***"
#define MQTT_PASSWORD "***"
#define MQTT_HOUSE "gong"


#define TTS_QRY_GOOGLE "http://translate.google.com/translate_tts?ie=UTF-8&tl=%1&client=tw-ob&ttsspeed=%2&q=%3"
#define TTS_LANG "de-DE"
#define TTS_SPEED "0.7"
#define TTS_MAX_LEN_TTM "50"



#endif