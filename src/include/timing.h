#ifndef TIMING_H
#define TIMING_H

#include <stdint.h> // uint64_t, uint32_t, uint8_t

typedef struct{
    uint64_t minutes; // 8 octets : bits 0–59 = minutes activées
    uint32_t heures;  // 4 octets : bits 0–23 = heures activées
    uint8_t jours;    // 1 octet  : bits 0–6  = jours activés
}Timing;

/*
 *   - Lit les 13 octets d'un fichier timing
 *   - Décode les valeurs en big-endian et renvoie une structure timing complète
 */
Timing lire_timing(const char *chemin);

/*
 *   - Affiche un timing décodé sous forme lisible avec jours/heures/minutes
 *   - Jours / heures / minutes activées
 */
void afficher_timing_generique(uint8_t jours, uint32_t heures, uint64_t minutes);

int doit_executer(const Timing *t, const struct tm *now);

#endif