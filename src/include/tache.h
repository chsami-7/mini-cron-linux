#ifndef TACHE_H 
#define TACHE_H

#include "timing.h"
#include "command.h"

// structure representant une tache chargee en memoire
typedef struct
{
    int id; // identifiant unique de la tache
    char path[2048]; // chemin absolu vers le dossier de la tache
    Timing timing; // structure definissant les horaires d'execution
    Command cmd; // structure definissant la commande a lancer
    time_t derniere_execution; // memoire de la derniere execution pour eviter les doublons
} Tache;

// lance l'execution de la commande dans le dossier specifie
int executer_tache(const char *task_dir, const Command *cmd);

// affiche les informations de la tache sur la sortie standard
void afficher_tache(uint64_t task_id, const Timing *timing, const Command *cmd);

// cree une nouvelle tache qui combine plusieurs taches existantes
uint64_t creer_tache_complexe(const char *tasks_dir, const Timing *t, uint16_t type_com, uint32_t nbtasks, uint64_t *ids);

#endif