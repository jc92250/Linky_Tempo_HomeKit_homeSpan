//
// ----------------------------------------------------------------
// Issues:
//
// TODO: au démarrage (par exemple, après une coupure de courant), il faut être en mode délestage
// TODO: led qui confirme le fonctionnement normal (homespan + bme280) ?
// TODO: fonctionnement en cas de 'plantage' de la partie HomeKit ?
// TODO: juste après l'upload du sketch, passer en chauffage ne se reset pas bien
//
// DONE: flash rapide de la led jaune lorsqu'une trame Linky est reçue
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
// Cette demande sera refusée en HPJR
// (elle est dédiée aux HPJW)
bool annulDelestage;

// depuis quand est-on en délestage pour cause de surconsommation ?
// le but est de ne pas annuler trop vite (pour tenir compte de l'apect
// intermittent de la surconsommation dans le cas d'un appareil
// électroménager)
uint32_t startDelestageSurConso;
// on ne s'intéressera à cette date que si on observe une surconsommation
bool delestageSurConso;


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

  // ESP Pins -----------------------------------------------------
  pinMode(PIN_SETUP, INPUT_PULLUP);  // bouton poussoir pour entrer en mode setup
  pinMode(PIN_PILOTE, OUTPUT);       // commande du fil pilote
  pinMode(LED_ANNUL, OUTPUT);        // la LED jaune qui indique qu'on a annulé le délestage
  pinMode(PIN_LED, OUTPUT);          // builtin led, utilisée par homeSpan

  digitalWrite(PIN_PILOTE, HIGH);
  Serial.println("Setup - Fil pilote = HIGH = chauffage coupé");

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
    // TODO: stop forever ?
  }
  //LED_HIST_ON();

   // init interface TIC
  tinfo.init(mode_tic);

  // soyons appelés lorsqu'on a reçu une nouvelle trame
  tinfo.attachADPS(AttachADPSCallback); // TODO: utile ?
  tinfo.attachUpdatedFrame(UpdatedFrameCallback);
  tinfo.attachData(AttachDataCallback);

  annulDelestage  = false;
  startDelestageSurConso = 0;
  delestageSurConso = false;

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

  // HomeSpan -----------------------------------------------------

  // Bouton poussoir pour annuler le délestage automatique
  // Premier appui: on annule le délestage (chauffage selon la programmation des radiateurs)
  // Second appui: on revient en mode automatique (délestage)
  pinMode(PIN_ANNUL_DELEST, INPUT_PULLUP);

  // Contrôle du device: un bouton et une LED
  homeSpan.setStatusPin(PIN_LED);    // led
  homeSpan.setControlPin(PIN_SETUP); // bouton
  homeSpan.setLogLevel(1);
  // Le WiFi interne utilisé lors de la configuration du device
  homeSpan.enableAutoStartAP();      // le mode point d'accès wifi sera actif automatiquement après un reset
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
  // on laisse tinfo essayer de décoder ce caractère
  if (Serial2.available()) {
    char c = Serial2.read();
    //Serial.print(c);
    tinfo.process(c);
  }

  // Sera utilisé pour savoir si on peut annuler le délestage
  // (utilisé uniquement en cas de surconsommation)
  uint32_t currentTime = millis();

  // l'utilisateur a demandé d'annuler le délestage ?
  // on mémorise cette demande et on verra ci-dessous si on peut la satisfaire
  bool oldAnnulDelestage = annulDelestage; // utilisé pour savoir si on a changé durant cette loop
  annulDelestage = thermostat->heatEnforced();
  digitalWrite(LED_ANNUL, annulDelestage ? HIGH : LOW);

  // Surconsommation ?
  // délestage forcé
  if (lkSurConsommation) {
    // Comme on veut être sûr de délester, on ne vérifie pas si on délestait déjà
    digitalWrite(PIN_PILOTE, HIGH);
    annulDelestage = false;
    thermostat->enforceAuto();
    // En on repousse la fin du délestage
    startDelestageSurConso = currentTime;
    delestageSurConso = true;
    return;
  }

  // Fin de surconsommation mais c'est trop tôt pour annuler le délestage
  // On reste donc en délestage
  if (delestageSurConso && (currentTime - startDelestageSurConso < TEMPS_DELESTAGE)) {
    // Comme on veut être sûr de délester, on ne vérifie pas si on délestait déjà
    digitalWrite(PIN_PILOTE, HIGH);
    annulDelestage = false;
    thermostat->enforceAuto();
    return;
  }

  // Pas de surconsommation actuelle ni récente
  delestageSurConso = false;

  // HPJR ?
  // délestage forcé
  if (lkPeriodeTempo == HPJR) {
    // On doit délester car on est en heure pleine de jour rouge
    digitalWrite(PIN_PILOTE, HIGH);
    annulDelestage = false;
    thermostat->enforceAuto();
    if (annulDelestage != oldAnnulDelestage) {
      Serial.println("Fil pilote = HIGH = chauffage coupé");
    }
    return;
  }

  // HPJW ?
  // on accepte un éventuel délestage
  if (lkPeriodeTempo == HPJW) {
    if (annulDelestage) {
      digitalWrite(PIN_PILOTE, LOW);
      if (annulDelestage != oldAnnulDelestage) {
        Serial.println("Fil pilote = LOW = chauffage actif");
      }
    } else {
      digitalWrite(PIN_PILOTE, HIGH);
      if (annulDelestage != oldAnnulDelestage) {
        Serial.println("Fil pilote = HIGH = chauffage coupé");
      }
    }
    return;
  }

  // OTHER ?
  // Pas de délestage dans les autres modes
  digitalWrite(PIN_PILOTE, LOW);
  annulDelestage = false;
  thermostat->enforceAuto();
  if (annulDelestage != oldAnnulDelestage) {
    Serial.println("Fil pilote = LOW = chauffage actif");
  }

}
