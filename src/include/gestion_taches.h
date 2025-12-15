#ifndef GESTION_TACHES_H
#define GESTION_TACHES_H

#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>     // Pour size_t
#include <getopt.h>     // Pour getopt

#include "tache.h"

// --- Prototypes des fonctions utilitaires ---

/** Initialise la gestion des signaux (SIGCHLD). */
void init_signals(void);

/** Calcule le temps de sommeil jusqu'à la prochaine minute complète. */
void compute_minute_timeout(struct timeval *tv);

/** Calcule l'horodatage de la minute actuelle (seconde 0). */
time_t get_current_minute(struct tm *now);

/** Charge toutes les tâches depuis le répertoire des tâches dans le tableau global. */
void load_tasks(const char *tasks_dir);

/** Obtient le répertoire d'exécution (via -r ou par défaut). */
const char *get_run_directory(int argc, char **argv);

/** Construit un chemin de répertoire et vérifie la taille. */
int make_dir_path(char *buf, size_t size, const char *base, const char *suffix);

/** Crée le répertoire 'pipes' et les FIFOs pour la communication. */
void create_pipes(const char *pipes_dir, char *req_pipe, size_t req_size, char *rep_pipe, size_t rep_size);

/** Boucle principale du serveur (Select + Traitement des requêtes/Planification des tâches). */
void server_loop(int fd_req, const char *rep_pipe, const char *tasks_dir);

#endif