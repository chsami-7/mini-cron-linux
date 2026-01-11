#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <getopt.h>

#include "gestion_taches.h"
#include "api_erraid.h"      
#include "timing.h"           
#include "command.h"          
#include "tache.h"            

// variables globales pour stocker les taches
extern Tache taches[2048];
extern int nb_taches;

int run_in_foreground = 0;
char *custom_pipes_dir = NULL; 


// configure les signaux pour eviter les processus zombies
void init_signals(void)
{
    signal(SIGCHLD, SIG_IGN);
}

// calcule le nombre de secondes a attendre avant la prochaine minute pile
void compute_minute_timeout(struct timeval *tv)
{
    time_t now_t = time(NULL);
    struct tm now;
    localtime_r(&now_t, &now);

    int seconds_to_sleep = 60 - now.tm_sec;
    if (seconds_to_sleep <= 0)
        seconds_to_sleep = 1;

    tv->tv_sec = seconds_to_sleep;
    tv->tv_usec = 0;
}

// recupere l'heure courante arrondie a la minute
time_t get_current_minute(struct tm *now)
{
    time_t now_t = time(NULL);
    localtime_r(&now_t, now);
    return now_t - now->tm_sec;
}

// analyse les arguments de la ligne de commande pour configurer les dossiers
const char *get_run_directory(int argc, char **argv)
{
    int opt;
    char *run_directory = NULL;

    static char default_run_dir[1024]; 
    const char *user = getenv("USER");
    if (!user) user = "default";
    snprintf(default_run_dir, sizeof(default_run_dir), "/tmp/%s/erraid", user);

    // gestion des options R P et F
    while ((opt = getopt(argc, argv, "R:P:F")) != -1)
    {
        switch (opt)
        {
        case 'R': 
            run_directory = optarg;
            break;
        case 'P': 
            custom_pipes_dir = optarg;
            break;
        case 'F':
            run_in_foreground = 1;
            break;
        default:
            fprintf(stderr, "Usage: %s [-R RUN_DIRECTORY] [-P PIPES_DIR] [-F]\n", argv[0]);
            exit(1);
        }
    }

    if (!run_directory) run_directory = default_run_dir;
    return run_directory;
}

// utilitaire pour construire des chemins de fichiers
int make_dir_path(char *buf, size_t size, const char *base, const char *suffix)
{
    if (snprintf(buf, size, "%s/%s", base, suffix) >= (int)size) return -1;
    return 0;
}

// cree les dossiers et les fichiers speciaux pour la communication
void create_pipes(const char *pipes_dir, char *req_pipe, size_t req_size, char *rep_pipe, size_t rep_size)
{
    if (mkdir(pipes_dir, 0777) == -1 && errno != EEXIST)
    {
        perror("mkdir pipes");
        exit(1);
    }

    if (make_dir_path(req_pipe, req_size, pipes_dir, "erraid-request-pipe") < 0 ||
        make_dir_path(rep_pipe, rep_size, pipes_dir, "erraid-reply-pipe") < 0)
    {
        fprintf(stderr, "Pipe path too long\n");
        exit(1);
    }

    // creation du tube de requete
    if (mkfifo(req_pipe, 0666) == -1 && errno != EEXIST)
    {
        perror("mkfifo req");
        exit(1);
    }

    // creation du tube de reponse
    if (mkfifo(rep_pipe, 0666) == -1 && errno != EEXIST)
    {
        perror("mkfifo rep");
        exit(1);
    }
}

// charge toutes les taches presentes sur le disque au demarrage
void load_tasks(const char *tasks_dir)
{
    DIR *d = opendir(tasks_dir);
    if (!d)
    {
        perror("Erreur ouverture tasks");
        exit(1);
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        char task_path[2048];
        snprintf(task_path, sizeof(task_path), "%s/%s", tasks_dir, entry->d_name);

        struct stat st;
        if (stat(task_path, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;

        // chargement du timing
        char timing_path[2048];
        if (snprintf(timing_path, sizeof(timing_path), "%s/timing", task_path) >= (int)sizeof(timing_path))
        {
            continue;
        }
        Timing t = lire_timing(timing_path);

        // chargement de la commande
        char cmd_path[2048];
        if (snprintf(cmd_path, sizeof(cmd_path),"%s/cmd", task_path) >= (int)sizeof(cmd_path)){
            continue;
        }
        Command c = lire_command(cmd_path);
        if (c.type == 0)
            continue;

        // ajout dans le tableau en memoire
        taches[nb_taches].id = atoi(entry->d_name);
        taches[nb_taches].timing = t;
        taches[nb_taches].cmd = c;

        strncpy(taches[nb_taches].path, task_path, sizeof(taches[nb_taches].path) - 1);
        taches[nb_taches].path[sizeof(taches[nb_taches].path) - 1] = '\0';

        taches[nb_taches].derniere_execution = 0;
        nb_taches++;
    }

    closedir(d);
}

// boucle principale du serveur qui attend les evenements
void server_loop(int fd_req, const char *rep_pipe, const char *tasks_dir)
{
    while (1)
    {
        struct timeval tv;
        compute_minute_timeout(&tv);

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd_req, &readfds);

        // attente passive jusqu'a une requete ou le timeout
        int ret = select(fd_req + 1, &readfds, NULL, NULL, &tv);

        // si on recoit une requete on la traite
        if (ret > 0 && FD_ISSET(fd_req, &readfds))
        {
            handle_request(fd_req, rep_pipe, tasks_dir);
            continue;
        }

        // verification des taches a lancer a chaque minute
        struct tm now;
        time_t minute_actuelle = get_current_minute(&now);

        static time_t derniere_verification = 0;

        if (derniere_verification != minute_actuelle)
        {
            for (int i = 0; i < nb_taches; i++)
            {
                if (doit_executer(&taches[i].timing, &now))
                {
                    if (taches[i].derniere_execution != minute_actuelle)
                    {
                        executer_tache(taches[i].path, &taches[i].cmd);
                        taches[i].derniere_execution = minute_actuelle;
                    }
                }
            }
            derniere_verification = minute_actuelle;
        }
    }
}