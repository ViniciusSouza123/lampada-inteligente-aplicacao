

#include <ArduinoIoTCloud.h>
#include <Arduino_ConnectionHandler.h>

const char DEVICE_LOGIN_NAME[]  = "SEU_DEVICE_ID";

const char SSID[]               = SECRET_SSID;    // Network SSID (name)
const char PASS[]               = SECRET_OPTIONAL_PASS;    // Network password (use for WPA, or use as key for WEP)
const char DEVICE_KEY[]  = SECRET_DEVICE_KEY;    // Secret device password

void onComandoChange();

String comando;
String LED;
String sistema;
float temperatura;
bool luminosidade;

void initProperties(){

  ArduinoCloud.setBoardId(DEVICE_LOGIN_NAME);
  ArduinoCloud.setSecretDeviceKey(DEVICE_KEY);
  ArduinoCloud.addProperty(comando, READWRITE, ON_CHANGE, onComandoChange);
  ArduinoCloud.addProperty(LED, READ, ON_CHANGE, NULL);
  ArduinoCloud.addProperty(sistema, READ, ON_CHANGE, NULL);
  ArduinoCloud.addProperty(temperatura, READ, ON_CHANGE, NULL);
  ArduinoCloud.addProperty(luminosidade, READ, ON_CHANGE, NULL);

}

WiFiConnectionHandler ArduinoIoTPreferredConnection(SSID, PASS);