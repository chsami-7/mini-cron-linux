#ifndef GESTION_TACHES_H
#define GESTION_TACHES_H

#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>     
#include <getopt.h>     

#include "tache.h"

// indique si on doit lancer le programme en premier plan
extern int run_in_foreground;

// contient le chemin vers les tubes nommes si l'option P est utilisee
extern char *custom_pipes_dir;

// configure les signaux pour eviter les processus zombies
void init_signals(void);

// calcule le temps a attendre avant la prochaine minute
void compute_minute_timeout(struct timeval *tv);

// recupere l'heure actuelle arrondie a la minute
time_t get_current_minute(struct tm *now);

// lit toutes les taches sur le disque et remplit le tableau global
void load_tasks(const char *tasks_dir);

// analyse les arguments pour determiner le dossier de travail
const char *get_run_directory(int argc, char **argv);

// utilitaire pour construire un chemin de fichier securise
int make_dir_path(char *buf, size_t size, const char *base, const char *suffix);

// cree le dossier des pipes et les fichiers fifo
void create_pipes(const char *pipes_dir, char *req_pipe, size_t req_size, char *rep_pipe, size_t rep_size);

// boucle principale qui gere les requetes et le lancement des taches
void server_loop(int fd_req, const char *rep_pipe, const char *tasks_dir);

#endif