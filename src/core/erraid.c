#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <endian.h>
#include <signal.h>
#include <errno.h>

#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>

#include "timing.h"
#include "command.h"
#include "tache.h"
#include "io_utils.h"    // Contient write_full, send_error
#include "api_erraid.h"  // Contient reponse_requete_list, etc.
#include "gestion_taches.h"

// Codes Opération
#define OP_LIST 0x4c53
#define OP_STDOUT 0x534f
#define OP_STDERR 0x5345
#define OP_TIMES 0x5458

Tache taches[2048];
int nb_taches = 0;

int main(int argc, char **argv)
{
    // Empêche les zombies
    struct sigaction act = {0};
    act.sa_handler = SIG_IGN;
    sigaction(SIGCHLD, &act, NULL);

    // Récupération du répertoire de travail
    const char *run_directory = get_run_directory(argc, argv);

    // Chargement de l'arborescence des tâches
    char tasks_dir[1024];
    int n_tasks = snprintf(tasks_dir, sizeof(tasks_dir), "%s/tasks", run_directory);
    if (n_tasks < 0 || n_tasks >= (int)sizeof(tasks_dir)) return 1;

    // Ouverture du répertoire des tâches
    DIR *d = opendir(tasks_dir);
    if (!d) {
        perror("Erreur ouverture tasks");
    } else {
        struct dirent *entry;
        while ((entry = readdir(d)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            
            char task_path[2048];
            snprintf(task_path, sizeof(task_path), "%s/%s", tasks_dir, entry->d_name);
            
            struct stat st;
            if (stat(task_path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
            
            // Lecture timing
            char timing_path[2048];
            snprintf(timing_path, sizeof(timing_path), "%s/%s/timing", tasks_dir, entry->d_name);
            Timing t = lire_timing(timing_path);
                    
            // Lecture commande
            char cmd_path[2048];
            snprintf(cmd_path, sizeof(cmd_path), "%s/%s/cmd", tasks_dir, entry->d_name);
            Command c = lire_command(cmd_path);
            
            if (c.type == 0) continue; // Commande invalide

            // Stockage en mémoire
            taches[nb_taches].id = atoi(entry->d_name);
            taches[nb_taches].timing = t;
            taches[nb_taches].cmd = c;
            strncpy(taches[nb_taches].path, task_path, sizeof(taches[nb_taches].path) - 1);
            taches[nb_taches].path[sizeof(taches[nb_taches].path) - 1] = '\0';
            taches[nb_taches].derniere_execution = 0;
            
            nb_taches++;
            if (nb_taches >= 2048) {
                fprintf(stderr, "Erreur : trop de tâches, limite atteinte.\n");
                break;
            }
        }
        closedir(d);
    }

    // Création du répertoire des tubes
    char pipes_dir[4096];
    snprintf(pipes_dir, sizeof(pipes_dir), "%s/pipes", run_directory);
    
    struct stat st;
    if (stat(pipes_dir, &st) == -1) {
        mkdir(pipes_dir, 0700);
    }

    char req_pipe[4200], rep_pipe[4200];
    snprintf(req_pipe, sizeof(req_pipe), "%s/erraid-request-pipe", pipes_dir);
    snprintf(rep_pipe, sizeof(rep_pipe), "%s/erraid-reply-pipe", pipes_dir);

    // Création des FIFO si nécessaire
    if (mkfifo(req_pipe, 0600) != 0 && errno != EEXIST) { perror("mkfifo req"); exit(1); }
    if (mkfifo(rep_pipe, 0600) != 0 && errno != EEXIST) { perror("mkfifo rep"); exit(1); }

    int fd_req = open(req_pipe, O_RDWR); 
    if (fd_req == -1) { perror("open req pipe"); exit(1); }

    while (1) {
        // Calcul du temps jusqu'à la prochaine minute
        time_t now_t = time(NULL);
        struct tm now;
        localtime_r(&now_t, &now);
        
        int seconds_to_sleep = 60 - now.tm_sec;
        if (seconds_to_sleep <= 0) seconds_to_sleep = 1;

        struct timeval tv;
        tv.tv_sec = seconds_to_sleep;
        tv.tv_usec = 0;

        // Configuration du multiplexage (select)
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd_req, &readfds);

        // Attente : soit requête, soit nouvelle minute
        int ret = select(fd_req + 1, &readfds, NULL, NULL, &tv);
        
        if (ret == -1) {
            if (errno == EINTR) continue;
            perror("select");
            continue;
        }

        // Cas 1 : Réveil par REQUÊTE sur le tube
        if (ret > 0 && FD_ISSET(fd_req, &readfds)) {
            uint16_t opcode;
            ssize_t lus = read(fd_req, &opcode, 2);
            
            if (lus == 2) {
                int fd_rep = open(rep_pipe, O_WRONLY);
                if (fd_rep != -1) {
                    opcode = be16toh(opcode);
                    switch (opcode) {
                        case OP_LIST:
                            // Appel fonction définie dans api_erraid.c
                            reponse_requete_list(fd_rep);
                            break;
                        case OP_STDOUT:
                            // Appel fonction définie dans api_erraid.c
                            reponse_requete_output(fd_req, fd_rep, tasks_dir, 0);
                            break;
                        case OP_STDERR:
                            // Appel fonction définie dans api_erraid.c
                            reponse_requete_output(fd_req, fd_rep, tasks_dir, 1);
                            break;
                        case OP_TIMES:
                            // Appel fonction définie dans api_erraid.c
                            reponse_requete_times(fd_req, fd_rep);
                            break;
                        default:
                            fprintf(stderr, "Opcode inconnu: %x\n", opcode);
                            send_error(fd_rep, 0x0000); // Défini dans io_utils.c
                            break;
                    }
                    close(fd_rep);
                }
            }
            continue; 
        }

        // Cas 2 : Réveil par TIMEOUT (nouvelle minute)
        now_t = time(NULL);
        localtime_r(&now_t, &now);
        time_t minute_actuelle = now_t / 60;
        
        static time_t derniere_verification = 0;

        if (derniere_verification != minute_actuelle) {
            for (int i = 0; i < nb_taches; i++) {
                if (doit_executer(&taches[i].timing, &now)) {
                    if (taches[i].derniere_execution != minute_actuelle) {
                        // Appel fonction définie dans gestion_taches.c (probablement)
                        executer_tache(taches[i].path, &taches[i].cmd); 
                        taches[i].derniere_execution = minute_actuelle;
                    }
                }
            }
            derniere_verification = minute_actuelle;
        }
    }

    for (int i = 0; i < nb_taches; i++)
        liberer_command(&taches[i].cmd);
    close(fd_req);
    return 0;
}