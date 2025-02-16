#include <pgmspace.h>
 
#define SECRET
#define THINGNAME ""                        
 
const char WIFI_SSID[] = "";               
const char WIFI_PASSWORD[] = "";        
const char AWS_IOT_ENDPOINT[] = "";      
 
// Amazon Root CA 1
static const char AWS_CERT_CA[] PROGMEM = "";
 
// Device Certificate                                               //change this
static const char AWS_CERT_CRT[] PROGMEM = "";
 
// Device Private Key                                               //change this
static const char AWS_CERT_PRIVATE[] PROGMEM = "";