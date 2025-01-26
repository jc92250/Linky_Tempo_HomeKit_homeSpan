# Linky_Tempo_HomeKit

## Introduction

Ceci est la "V2" du projet Linky_Tempo:
L'objectif est de pouvoir accéder au status du délestage ainsi qu'à la possibilité d'annuler ce délestage depuis Internet.

## Le Contexte

Le logement est en tout électrique
Le contrat EDF est en offre tempo
En général, hors télétravail, l'appartement est vide dans la journée
L'environnement informatique est orienté Apple (mac, iPhone, appleTV)

## Rappel du Besoin

Le besoin est de délester le chauffage (le basculer en hors-gel) automatiquement:
- en heure pleine jour rouge (HPJR) et jour blanc (HPJW)
- en cas de surconsommation

A la demande, il faut pouvoir annuler le délestage en HPJW (mais pas en HPJR)
Cette annulation du délestage doit pouvoir se faire même si on se trouve hors de la maison
 
## L'Approche Suivie

Pra rapport au projet Linky_Tempo, l'approche suivie sera de:
- remplacer l'Arduino par un ESP32
- installer HomeSpan sur l'ESP32 pour qu'il soit vu comme un device HomeKit
- le configurer comme un "Thermostat"

Pourquoi "Thermostat" ?  
Car cela semble être le device dont les fonctionnalités sont les plus proches de celles qu'on veut obtenir.  
Voici ses différents modes de fonctionnement (CurrentHeatingCoolingState) et leur usage prévu:
- IDLE: utilisé, délestage possible en fonction des infos du Linky
- HEATING: délestage annulé, chauffage actif
- COOLING: pas utilisé

Concernant la commande (TargetHeatingCoolingState) on a:
- OFF: pas utilisé
- HEAT: annulation du délestage, activer le chauffage (sauf si surconsommation)
- COOL: pas utilisé
- AUTO: mode automatique (délestage HPJR, HPJW et surconsommation)

Le circuit a également un bouton poussoir permettant de basculer entre le mode automatique et le mode annulation du délestage

Note: "Heater-Cooler" aurait peut-être pu être utilisé à la place de "Thermostat". Mais cette possibilité n'a pas été étudiée.

## Configuration

Factory reset:  
Faire un appui long (> 10 secondes) sur le bouton reset. Au bout de 3 secondes, la led interne va clignoter rapidement, continuer l'appui sur le bouton jusqu'à l'extinction de la led.

A ce moment, le device est configuré comme un point d'accès wifi sur lequel on peut se connecter avec un ordinateur ou un smartphone. Voici les paramètres du point d'accès:
- nom du wifi: JCDELESTAGE
- mot de passe: 123456789

Via ce point d'accès, il est possible de:
- définir le réseau wifi sur lequel le device doit se connecter
- définir le setup-code homeKit du device

Une fois ces informations saisies, le device va redémarrer et ce connecteur à ce
réseau wifi. Une fois ceci effectué, la led bleue va flasher 2 fois toutes les 3 secondes
pour indiquer qu'il n'est pas pairé dans Maison.

## Les LEDs

LED rouge: toujours allumée, indique que l'ESP32 est alimenté  
LED bleue:  
- allumée fixe: le device est en fonctionnement et pairé dans Maison  
- allumée et clignotante (2 flashs brefs toutes les 3 secondes): le device n'est pas pairé dans Maison
