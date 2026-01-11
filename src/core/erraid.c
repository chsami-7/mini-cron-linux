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

// codes des differentes operations possibles
#define OP_LIST 0x4c53
#define OP_STDOUT 0x534f
#define OP_STDERR 0x5345
#define OP_TIMES 0x5458
#define OP_TERMINATE 0x4b49 

Tache taches[2048];
int nb_taches = 0;

extern int run_in_foreground;
extern char *custom_pipes_dir; 

int main(int argc, char **argv)
{
    // on ignore sigchld pour eviter les zombies
    struct sigaction act = {0};
    act.sa_handler = SIG_IGN;
    sigaction(SIGCHLD, &act, NULL);

    // recuperation du dossier de travail et des options
    const char *run_directory = get_run_directory(argc, argv);

    // gestion du lancement en arriere plan ou non
    if (run_in_foreground) {
        fprintf(stderr, "erraid: running in foreground (no daemonisation)\n");
    } else {
        pid_t pid = fork();
        if (pid == -1) { perror("fork"); return 1; }
        if (pid > 0) {
            fprintf(stderr, "erraid: started daemon (pid %d)\n", pid);
            return 0;
        }
        // creation d'une nouvelle session pour le demon
        if (setsid() == -1) { perror("setsid");}

        // redirection des entrees sorties vers dev null
        int fd = open("/dev/null", O_RDWR);
        if (fd != -1) {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2) close(fd);
        }
    }

    // construction du chemin vers le dossier tasks
    char tasks_dir[1024];
    int n_tasks = snprintf(tasks_dir, sizeof(tasks_dir), "%s/tasks", run_directory);
    if (n_tasks < 0 || n_tasks >= (int)sizeof(tasks_dir)) return 1;

    // creation des dossiers si necessaire
    struct stat st_dir;
    if (stat(run_directory, &st_dir) == -1) {
        mkdir(run_directory, 0777);
    }
    if (stat(tasks_dir, &st_dir) == -1) {
        mkdir(tasks_dir, 0777);
    }

    // ouverture du dossier pour lister les taches existantes
    DIR *d = opendir(tasks_dir);
    if (!d) {
        perror("Erreur ouverture tasks");
    } else {
        struct dirent *entry;
        // lecture de chaque sous-dossier
        while ((entry = readdir(d)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            
            char task_path[2048];
            snprintf(task_path, sizeof(task_path), "%s/%s", tasks_dir, entry->d_name);
            
            struct stat st;
            if (stat(task_path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

            // chargement des infos de timing
            char timing_path[2048];
            snprintf(timing_path, sizeof(timing_path), "%s/%s/timing", tasks_dir, entry->d_name);
            Timing t = lire_timing(timing_path);
                    
            // chargement de la commande
            char cmd_path[2048];
            snprintf(cmd_path, sizeof(cmd_path), "%s/%s/cmd", tasks_dir, entry->d_name);
            Command c = lire_command(cmd_path);
            
            if (c.type == 0) continue; 

            // ajout de la tache chargee dans le tableau en memoire
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

    char pipes_dir[4096];

    // determination du dossier des pipes selon les options
    if (custom_pipes_dir != NULL) {
        strncpy(pipes_dir, custom_pipes_dir, sizeof(pipes_dir) - 1);
        pipes_dir[sizeof(pipes_dir) - 1] = '\0';
    } else {
        snprintf(pipes_dir, sizeof(pipes_dir), "%s/pipes", run_directory);
    }

    // creation du dossier pipes si besoin
    struct stat st;
    if (stat(pipes_dir, &st) == -1) {
        if (mkdir(pipes_dir, 0777) == -1 && errno != EEXIST) {
            perror("mkdir pipes");
            exit(1);
        }
    }

    char req_pipe[4200], rep_pipe[4200];
    snprintf(req_pipe, sizeof(req_pipe), "%s/erraid-request-pipe", pipes_dir);
    snprintf(rep_pipe, sizeof(rep_pipe), "%s/erraid-reply-pipe", pipes_dir);

    // creation des fichiers fifo pour communiquer
    if (mkfifo(req_pipe, 0600) != 0 && errno != EEXIST) { perror("mkfifo req"); exit(1); }
    if (mkfifo(rep_pipe, 0600) != 0 && errno != EEXIST) { perror("mkfifo rep"); exit(1); }

    int fd_req = open(req_pipe, O_RDWR); 
    if (fd_req == -1) { perror("open req pipe"); exit(1); }

    int running=1;
    // boucle principale du serveur
    while (running) {
        // calcul du temps restant avant la prochaine minute
        time_t now_t = time(NULL);
        struct tm now;
        localtime_r(&now_t, &now);
        
        int seconds_to_sleep = 60 - now.tm_sec;
        if (seconds_to_sleep <= 0) seconds_to_sleep = 1;

        struct timeval tv;
        tv.tv_sec = seconds_to_sleep;
        tv.tv_usec = 0;

        // preparation du select pour attendre
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd_req, &readfds);

        // attente soit d'une requete soit du timeout
        int ret = select(fd_req + 1, &readfds, NULL, NULL, &tv);

        if (ret == -1) {
            if (errno == EINTR) continue;
            perror("select");
            continue;
        }

        // cas reception d'une requete client
        if (ret > 0 && FD_ISSET(fd_req, &readfds)) {
            int arret = handle_request(fd_req, rep_pipe, tasks_dir);
            if (arret) {
                fprintf(stderr, "Arrêt du démon demandé\n");
                running = 0;
            }
            continue;
        }

        // cas nouvelle minute pour executer les taches
        now_t = time(NULL);
        localtime_r(&now_t, &now);
        time_t minute_actuelle = now_t / 60;
        
        static time_t derniere_verification = 0;

        // verification des taches a lancer si on a change de minute
        if (derniere_verification != minute_actuelle) {
            for (int i = 0; i < nb_taches; i++) {
                if (doit_executer(&taches[i].timing, &now)) {
                    if (taches[i].derniere_execution != minute_actuelle) {
                        executer_tache(taches[i].path, &taches[i].cmd);
                        taches[i].derniere_execution = minute_actuelle;
                    }
                }
            }
            derniere_verification = minute_actuelle;
        }
    }

    // nettoyage avant de quitter
    for (int i = 0; i < nb_taches; i++)
        liberer_command(&taches[i].cmd);

    close(fd_req);
    unlink(req_pipe);
    unlink(rep_pipe);
    return 0;
}