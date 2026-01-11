#ifndef TIMING_H
#define TIMING_H

#include <stdint.h> // uint64_t, uint32_t, uint8_t
#include <time.h>

// structure qui definit quand la tache doit se lancer avec des masques binaires
typedef struct{
    uint64_t minutes; // 8 octets : bits 0–59 = minutes activées
    uint32_t heures;  // 4 octets : bits 0–23 = heures activées
    uint8_t jours;    // 1 octet  : bits 0–6  = jours activés
}Timing;

// lit le fichier binaire timing sur le disque et remplit la structure
Timing lire_timing(const char *chemin);

// affiche les details des horaires de facon lisible pour le debug
void afficher_timing_generique(uint8_t jours, uint32_t heures, uint64_t minutes);

// verifie si la tache doit etre lancee a l'heure actuelle
int doit_executer(const Timing *t, const struct tm *now);

// ecrit la structure timing dans un fichier ou un tube
int serialise_timing(int fd,const Timing *t);

// lit la structure timing depuis un fichier ou un tube
Timing deserialise_timing(int fd);

// transforme une liste sous forme de texte en masque binaire
uint64_t parse_list_to_bitmap(const char *arg, int min_val, int max_val);

#endif