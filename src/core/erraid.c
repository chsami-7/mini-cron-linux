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

#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "timing.h"
#include "command.h"

typedef struct
{
    int id;
    char path[2048];
    Timing timing;
    Command cmd;
    time_t derniere_execution;
} Tache;

Tache taches[2048];
int nb_taches = 0;

/**
 * Exécute récursivement une commande par un processus fils via fork
 */

static int execute_recursive(const Command *cmd)
{
    // Commande Simple
    if (cmd->type == ('S' << 8 | 'I'))
    {
        if (cmd->args.argc == 0)
        {
            // Cas où c'est vide
            return 0;
        }

        pid_t pid = fork();
        if (pid == -1)
        {
            perror("fork (execute_recursive simple)");
            return -1; // Erreur de fork
        }

        if (pid == 0)
        {
            // on exec la commande fils
            char *argv[cmd->args.argc + 1];
            for (uint32_t i = 0; i < cmd->args.argc; i++)
            {
                argv[i] = cmd->args.argv[i];
            }
            argv[cmd->args.argc] = NULL;

            execvp(argv[0], argv);
            perror("execvp");
            _exit(127); // convention erreur non trouvée
        }

        int status;
        waitpid(pid, &status, 0);
        return status;
    }

    // Séquence de commandes
    if (cmd->type == ('S' << 8 | 'Q'))
    {
        int last_status = 0;
        for (uint32_t i = 0; i < cmd->nbcmds; i++)
        {
            // Appel récursif pour chaque sous-commande et on attend que la première soit finie avant d'execut la suivante
            last_status = execute_recursive(&cmd->cmds[i]);
        }
        return last_status;
    }

    // Autre cas : echec
    int status;
    pid_t pid = fork();
    if (pid == -1)
    {
        perror("fork (commande inconnue)");
        return -1;
    }
    if (pid == 0)
        _exit(1);
    waitpid(pid, &status, 0);
    return status;
}

int executer_tache(const char *task_dir, const Command *cmd)
{
    pid_t pid = fork();
    // Cas où le fork éechoue
    if (pid == -1)
    {
        perror("fork (executer_tache)");
        return -1; 
    }

    if (pid == 0)
    {
        // 1. Redirections stdout/stderr
        char outp[512], errp[512];
        snprintf(outp, sizeof(outp), "%s/stdout", task_dir);
        snprintf(errp, sizeof(errp), "%s/stderr", task_dir);

        // O_TRUNC car on veut la sortie de la dernière exécution
        int fd_out = open(outp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        int fd_err = open(errp, O_WRONLY | O_CREAT | O_TRUNC, 0644);

        if (fd_out == -1 || fd_err == -1)
        {
            perror("open (redirection)");
            _exit(1); // Echec
        }

        dup2(fd_out, STDOUT_FILENO);
        dup2(fd_err, STDERR_FILENO);
        close(fd_out);
        close(fd_err);

        int status = execute_recursive(cmd);

        if (WIFEXITED(status))
        {
            _exit(WEXITSTATUS(status));
        }
        else
        {
            _exit(255); // convention
        }
    }

    //  Processus Père 
    int status;
    waitpid(pid, &status, 0);

    // ON enregistre le résultat
    uint16_t exitcode;
    if (WIFEXITED(status))
    {
        exitcode = WEXITSTATUS(status);
        if (exitcode == 255)
        {
            exitcode = 0xFFFF;
        }
    }
    else
    {
        exitcode = 0xFFFF;
    }

    char logp[512];
    snprintf(logp, sizeof(logp), "%s/times-exitcodes", task_dir);

    int fdlog = open(logp, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fdlog == -1)
    {
        perror("open (times-exitcodes)");
        return -1; // Echec écriture log
    }

    int64_t t = htobe64(time(NULL));
    uint16_t code_be = htobe16(exitcode);

    // TODO c'est pas garanti que write écrive tous les octets d'un coup, il faut faire une version qui vérifie
    write(fdlog, &t, 8);
    write(fdlog, &code_be, 2);

    close(fdlog);

    return 0;
}

int main(int argc, char **argv)
{
    char *run_directory = NULL;
    int opt;

    // val par défaut
    char default_run_dir[1024];
    const char *user = getenv("USER");
    if (!user)
        user = "default";
    snprintf(default_run_dir, sizeof(default_run_dir), "/tmp/%s/erraid", user);

    while ((opt = getopt(argc, argv, "r:")) != -1)
    {
        switch (opt)
        {
        case 'r':
            run_directory = optarg;
            break;
        default:
            fprintf(stderr, "Usage: %s [-r RUN_DIRECTORY]\n", argv[0]);
            return 1;
        }
    }

    if (!run_directory)
    {
        run_directory = default_run_dir;
    }

    // construction du chemin
    char tasks_dir[1024];
    int n_tasks = snprintf(tasks_dir, sizeof(tasks_dir), "%s/tasks", run_directory);
    if (n_tasks < 0 || n_tasks >= (int)sizeof(tasks_dir)) {
        fprintf(stderr, "Erreur: Chemin des tâches trop long: %s/tasks\n", run_directory);
        return 1; 
    }

    DIR *d = opendir(tasks_dir);
    if (!d)
    {
        perror("Erreur ouverture tasks");
        return 1;
    }

    struct dirent *entry;
    // Parcours des entrées du dossier
    while ((entry = readdir(d)) != NULL)
    {
        // On ne prend pas en compte . et ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        // On vérifie si c'est un repo
        char task_path[2048];
        int n = snprintf(task_path, sizeof(task_path), "%s/%s", tasks_dir, entry->d_name);
        if (n < 0 || n >= (int)sizeof(task_path))
        {
            fprintf(stderr, "Erreur chemin de tâche trop long: %s/%s\n", tasks_dir, entry->d_name);
            continue;
        }

        struct stat st;
        if (stat(task_path, &st) != 0 || !S_ISDIR(st.st_mode))
        {
            continue;
        }

        char timing_path[2048];
        // On construit le chemin
        n = snprintf(timing_path, sizeof(timing_path), "%s/%s/timing", tasks_dir, entry->d_name);
        if (n < 0 || n >= (int)sizeof(timing_path))
        {
            fprintf(stderr, "Erreur chemin timing trop long: %s/%s/timing\n", tasks_dir, entry->d_name);
            continue;
        }

        // Lecture du timing 
        Timing t = lire_timing(timing_path);

        /* On vérifie si le timing est non-nul (sinon c'est une tâche "abstraite" du jalon 2, on ne la charge pas pour exécution)
        Concernant cette vérification, on attend juste qu'au moins un des champs soit non nul 
        TODO : améliorer cette vérification si besoin */
        if (t.minutes == 0 && t.heures == 0 && t.jours == 0)
        {
            // printf("Tâche %s ignorée (timing vide)\n", entry->d_name);
            continue;
        }

        // printf("\nChargement Tâche %s :\n", entry->d_name);
        // afficher_timing_generique(t.jours, t.heures, t.minutes);

        char cmd_path[2048];
        // --- CORRECTION ---
        // Idem ici
        n = snprintf(cmd_path, sizeof(cmd_path), "%s/%s/cmd", tasks_dir, entry->d_name);
        if (n < 0 || n >= (int)sizeof(cmd_path))
        {
            fprintf(stderr, "Erreur chemin cmd trop long: %s/%s/cmd\n", tasks_dir, entry->d_name);
            continue;
        }

        Command c = lire_command(cmd_path);
        if (c.type == 0)
        { // Echec de lecture de la commande
            fprintf(stderr, "Erreur impossible de lire la commande pour la tâche %s\n", entry->d_name);
            continue;
        }

        // On stocke la tâche
        taches[nb_taches].id = atoi(entry->d_name);
        taches[nb_taches].timing = t;
        taches[nb_taches].cmd = c;
        strncpy(taches[nb_taches].path, task_path, sizeof(taches[nb_taches].path) - 1);
        taches[nb_taches].path[sizeof(taches[nb_taches].path) - 1] = '\0'; // Pour voir si ça finit bien
        taches[nb_taches].derniere_execution = 0;                          // On force la réexécution si besoin
        nb_taches++;
    }

    closedir(d);

    if (nb_taches == 0)
    {
        fprintf(stderr, "Aucune tâche à exécuter n'a été trouvée dans %s \n", tasks_dir);
    }

    // Boucle infinie
    while (1)
    {
        // On attend le début de la minute suivante pour se synchroniser
        time_t now_t = time(NULL);
        struct tm now;
        localtime_r(&now_t, &now);
        
        int to_sleep = 60 - now.tm_sec;
        // On dort au minimum 1 seconde pour éviter une boucle trop rapide si l'exécution prend 0 sec
        // Donc on attend le début de la minute suivante
        if (to_sleep > 0) {
            sleep(to_sleep);
        }

        now_t = time(NULL);
        localtime_r(&now_t, &now);
        time_t minute_actuelle = now_t - now.tm_sec;

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
    }

    // Libérer mémoire secours
    for (int i = 0; i < nb_taches; i++)
    {
        liberer_command(&taches[i].cmd);
    }

    return 0;
}