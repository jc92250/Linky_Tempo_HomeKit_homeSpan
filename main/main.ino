//
// ----------------------------------------------------------------
// Issues:
//
// TODO: led qui confirme le fonctionnement normal (homespan + bme280) ?
// TODO: fonctionnement en cas de 'plantage' de la partie HomeKit ?
// TODO: juste après l'upload du sketch, passer en chauffage ne se reset pas bien
//
// DONE: activer le hotspot wifi immédiatement après un reset
// DONE: la tempo de délestage ne doit s'appliquer qu'à la surconsommation
// DONE: en délestage interdit, la température reste parfois à 21°C
// DONE: en délestage interdit, on reste parfois en mode chauffage
// ----------------------------------------------------------------
//
#include "constants.h"
//#include "wifi_JC.h"

// ----------------------------------------------------------------
// Téléinformation Linky
// Important: la téléinformation est reçue sur Serial2 = UART2
// (on réserve Serial = UART0 pour la programmation de l'ESP et
// les logs)
// ----------------------------------------------------------------
#include "LibTeleinfo.h"
#include "setup_detect_TIC_mode.h"
#include "callbacks.h"

// Dans quel mode est la TIC (standard ou historique ?) 
_Mode_e mode_tic;

// L'objet qui permet de récupérer le détail de la TIC
// Plus d'info ici: https://hallard.me/libteleinfo/
TInfo tinfo;

// L'utilisateur a-t-il demandé une annulation du délestage ?
bool annulDelestageHPJW;

// depuis quand est-on en délestage pour cause de surconsommation ?
// le but est de ne pas annuler trop vite (pour tenir compte de l'apect
// intermittent de la surconsommation dans le cas d'un appareil
// électroménager)
uint32_t startDelestage;


// ----------------------------------------------------------------
// Capteur de température et humidité BME280
// ----------------------------------------------------------------
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

Adafruit_BME280 bme280; // I2C
boolean bHasBME280 = true;


// ----------------------------------------------------------------
// HomeSpan (intégration HomeKit)
// ----------------------------------------------------------------
#include "HomeSpan.h"
#include "DEV_Thermostat.h"

DEV_Thermostat *thermostat;


// ----------------------------------------------------------------
// setup
// ----------------------------------------------------------------
void setup() {
  Serial.begin(9600);

  // Linky --------------------------------------------------------
  // init interface série utilisée pour récupérer la TIC
  // Note: la méthode force le mode historique car:
  // - on sait qu'on est en mode historique
  // - on ne veut pas prendre le risque que le code bascule en standard si la détection du mode historique échoue
	mode_tic = setup_detect_TIC_mode();

  // On ne supporte que le mode "historique"
  // Si on n'arrive pas à détecter ce mode, on ne peut rien faire
  if (mode_tic != TINFO_MODE_HISTORIQUE) {
    // On ne supporte pas le mode standard
    //LED_STD_ON();
    // On se met en mode Hors-gel pour limiter la consommation
    digitalWrite(PIN_PILOTE, HIGH); // TODO: LOW or HIGH
    // Et la LED rouge va flasher indéfiniment pour montrer le problème
    //LED_Flashx2Forever();
  }
  //LED_HIST_ON();

   // init interface TIC
  tinfo.init(mode_tic);

  // soyons appelés lorsqu'on a reçu une nouvelle trame
  tinfo.attachADPS(AttachADPSCallback); // TODO: utile ?
  tinfo.attachUpdatedFrame(UpdatedFrameCallback);
  tinfo.attachData(AttachDataCallback);

  annulDelestageHPJW  = false;

  // BME280, with default settings --------------------------------
  unsigned status = bme280.begin(0x76);
  if (!status) {
    Serial.println("Could not find a valid BME280 sensor, check wiring, address, sensor ID!");
    Serial.print("SensorID was: 0x"); Serial.println(bme280.sensorID(),16);
    Serial.println("        ID of 0xFF probably means a bad address, a BMP 180 or BMP 085");
    Serial.println("   ID of 0x56-0x58 represents a BMP 280.");
    Serial.println("        ID of 0x60 represents a BME 280.");
    Serial.println("        ID of 0x61 represents a BME 680.");
    bHasBME280 = false;
  }

  // Commandes ----------------------------------------------------
  pinMode(PIN_SETUP, INPUT_PULLUP);  // bouton poussoir pour entrer en mode setup
  pinMode(LED_ANNUL, OUTPUT);        // la LED jaune qui indique qu'on a annulé le délestage
  pinMode(PIN_LED, OUTPUT);          // builtin led, utilisée par homeSpan

  // Bouton poussoir pour annuler le délestage automatique
  // Premier appui: on annule le délestage (chauffage selon la programmation des radiateurs)
  // Second appui: on reviens en mode automatique (délestage)
  pinMode(PIN_ANNUL_DELEST, INPUT_PULLUP);

  // HomeSpan -----------------------------------------------------

  // Controle du device: un bouton et une LED
  homeSpan.setStatusPin(PIN_LED);   // led
  homeSpan.setControlPin(PIN_SETUP); // bouton
  //homeSpan.setLogLevel(2);
  // Le WiFi interne utilisé lors de la configuration du device
  homeSpan.enableAutoStartAP(); // le mode point d'accès wifi sera actif automatiquement après un reset
  homeSpan.setApSSID(APSSID);
  homeSpan.setApPassword(APPASSWORD);
  // TEMPORAIRE:
  //homeSpan.setWifiCredentials(WIFI_SSID, WIFI_PASSWORD);
  //homeSpan.setPairingCode(PAIRING_CODE);
  // Définition de l'accessoire
  homeSpan.begin(Category::Thermostats, DISPLAY_NAME, HOST_NAME_BASE, MODEL_NAME);

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  thermostat = new DEV_Thermostat(bHasBME280 ? &bme280 : nullptr);

  homeSpan.autoPoll();
}


// ----------------------------------------------------------------
// loop
// ----------------------------------------------------------------
void loop() {
  // réception d'un caractère de la TIC
  // on laisse tinfo essayer de décoder l'ensemble
  if (Serial2.available()) {
    char c = Serial2.read();
    //Serial.print(c);
    tinfo.process(c);
  }

  // Délestage
  uint32_t currentTime = millis();

  // l'utilisateur a demandé d'annuler le délestage ?
  annulDelestageHPJW = thermostat->heatEnforced();
  digitalWrite(LED_ANNUL, annulDelestageHPJW ? HIGH : LOW);

  // On ne peut annuler le délestage qu'en HPJW
  // On s'assure donc que cette annulation ne reste pas active trop longtemps
  if (annulDelestageHPJW && !delestage_HPJW) {
    thermostat->enforceAuto();
    annulDelestageHPJW = false;
    // La LED_ANNUL sera éteinte au prochain passage dans la loop()
  }

  // on indique au fil pilote si on déleste ou pas
  if (delestage_CONSO) {
    // On doit délester pour cause de surconsommation
    // Comme on ne sait pas si on était déjà en mode délestage, on définit les pins de sortie en conséquence
    digitalWrite(PIN_PILOTE, HIGH);
    // En on repousse la fin du délestage
    startDelestage = currentTime;

  } else if (delestage_HPJR) {
    // On doit délester pour cause de jour rouge
    // Comme on ne sait pas si on était déjà en mode délestage, on définit les pins de sortie en conséquence
    digitalWrite(PIN_PILOTE, HIGH);

  } else if (delestage_HPJW && !annulDelestageHPJW) {
    // On doit délester pour cause de jour blanc
    // Comme on ne sait pas si on était déjà en mode délestage, on définit les pins de sortie en conséquence
    digitalWrite(PIN_PILOTE, HIGH);

  } else if (currentTime - startDelestage > TEMPS_DELESTAGE) {
    // On n'a plus besoin de délester
    // et ceci, depuis assez longtemps (TEMPS_DELESTAGE)
    // Ce temps d'attente est pour éviter de basculer trop souvent
    // Par exemple, lorsque le four ou une plaque de cuisson est en maintien de température
    // On peut enfin annuler le délestage
    // TODO: LED_Eteinte();
    digitalWrite(PIN_PILOTE, LOW);
  }
}
