#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <endian.h>
#include <signal.h> 
#include <string.h>
#include <stdlib.h> 
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>

#include "command.h"

// fonction qui execute une commande de n'importe quel type de facon recursive
static int execute_recursive(const Command *cmd)
{
    // cas commande simple
    if (cmd->type == ('S' << 8 | 'I'))
    {
        if (cmd->args.argc == 0) return 0;
        pid_t pid = fork();
        if (pid == -1) return -1;
        
        // processus fils qui execute le programme
        if (pid == 0) {
            char *argv[cmd->args.argc + 1];
            for (uint32_t i = 0; i < cmd->args.argc; i++) argv[i] = cmd->args.argv[i];
            argv[cmd->args.argc] = NULL;
            execvp(argv[0], argv);
            _exit(127);
        }
        
        // le pere attend la fin de l'execution
        int status;
        waitpid(pid, &status, 0);
        return status;
    }
    // cas sequence de commandes
    else if (cmd->type == ('S' << 8 | 'Q')) {
        int last_status = 0;
        for (uint32_t i = 0; i < cmd->nbcmds; i++) {
            if (cmd->cmds[i].type == 0) continue;
            // appel recursif pour chaque sous-commande
            last_status = execute_recursive(&cmd->cmds[i]);
        }
        return last_status;
    }
    // cas pipeline
    else if (cmd->type == ('P' << 8 | 'L')) {
        if (cmd->nbcmds == 0) return 0;
        
        int prev_fd = -1;
        int status = 0;
        pid_t *pids = malloc(cmd->nbcmds * sizeof(pid_t));
        
        // creation des processus pour chaque etage du pipe
        for (uint32_t i = 0; i < cmd->nbcmds; i++) {
            int pipefd[2];
            // creation du tube sauf pour le dernier element
            if (i < cmd->nbcmds - 1) {
                if (pipe(pipefd) == -1) {
                    free(pids);
                    return -1;
                }
            }

            pid_t pid = fork();
            if (pid == 0) {
                // connexion de l'entree standard au tube precedent
                if (prev_fd != -1) {
                    dup2(prev_fd, STDIN_FILENO);
                    close(prev_fd);
                }
                // connexion de la sortie standard au tube suivant
                if (i < cmd->nbcmds - 1) {
                    dup2(pipefd[1], STDOUT_FILENO);
                    close(pipefd[0]);
                    close(pipefd[1]);
                }
                
                int sub_status = execute_recursive(&cmd->cmds[i]);
                if (WIFEXITED(sub_status)) _exit(WEXITSTATUS(sub_status));
                else _exit(1);
            }
            
            pids[i] = pid;
            // fermeture des descripteurs inutiles dans le pere
            if (prev_fd != -1) close(prev_fd);
            if (i < cmd->nbcmds - 1) {
                close(pipefd[1]);
                prev_fd = pipefd[0];
            }
        }

        // attente de tous les fils
        for (uint32_t i = 0; i < cmd->nbcmds; i++) {
            int wstatus;
            waitpid(pids[i], &wstatus, 0);
            if (i == cmd->nbcmds - 1) status = wstatus;
        }
        free(pids);
        return status;
    }
    // cas conditionnelle if then else
    else if (cmd->type == ('I' << 8 | 'F')) {
        if (cmd->nbcmds < 2) return -1;
        
        // execution de la condition
        int cond_status = execute_recursive(&cmd->cmds[0]);
        
        // si succes on execute le then sinon le else
        if (WIFEXITED(cond_status) && WEXITSTATUS(cond_status) == 0) {
            return execute_recursive(&cmd->cmds[1]);
        } else {
            if (cmd->nbcmds >= 3) {
                return execute_recursive(&cmd->cmds[2]);
            }
        }
        return 0;
    }
    return -1;
}

// lance l'execution d'une tache complete et gere les logs
int executer_tache(const char *task_dir, const Command *cmd)
{
    pid_t pid = fork();
    if (pid == -1) { perror("fork"); return -1; }

    if (pid == 0) {
        // retablit le comportement par defaut des signaux
        signal(SIGCHLD, SIG_DFL); 
        
        time_t start_time = time(NULL);

        // preparation des fichiers de logs
        char outp[512], errp[512];
        snprintf(outp, sizeof(outp), "%s/stdout", task_dir);
        snprintf(errp, sizeof(errp), "%s/stderr", task_dir);

        int fd_out = open(outp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        int fd_err = open(errp, O_WRONLY | O_CREAT | O_TRUNC, 0644);

        // redirection des sorties
        if (fd_out != -1 && fd_err != -1) {
            dup2(fd_out, STDOUT_FILENO);
            dup2(fd_err, STDERR_FILENO);
            close(fd_out);
            close(fd_err);
        }

        // execution reelle de la commande
        int status = execute_recursive(cmd);

        uint16_t exitcode;
        if (WIFEXITED(status)) {
            int ret = WEXITSTATUS(status);
            exitcode = (ret == 255) ? 0xFFFF : (uint16_t)ret;
        } else {
            exitcode = 0xFFFF;
        }

        // ecriture du code de retour et de la date dans le fichier de log
        char logp[512];
        snprintf(logp, sizeof(logp), "%s/times-exitcodes", task_dir);
        int fdlog = open(logp, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fdlog != -1) {
            int64_t t = htobe64(start_time);
            uint16_t code_be = htobe16(exitcode);
            write(fdlog, &t, 8);
            write(fdlog, &code_be, 2);
            close(fdlog);
        }

        _exit(0);
    }
    
    return 0;
}

// affiche une plage de valeurs comme 1-5 ou 1,3,5
void print_range(uint64_t mask, int max_val) {
    if (mask == 0) { printf("- "); return; }
    
    uint64_t full_mask = 0;
    int limit = (max_val == 59) ? 60 : max_val + 1;
    for(int i=0; i < limit; i++) full_mask |= (1ULL << i);
    
    // si tout est selectionne on affiche etoile
    if ((mask & full_mask) == full_mask) { printf("* "); return; }
    
    int first = 1;
    int i = 0;
    while (i <= max_val) {
        if ((mask >> i) & 1) {
            int start = i;
            // cherche la fin de la plage consecutive
            while ((i + 1 <= max_val) && ((mask >> (i + 1)) & 1)) i++;
            int end = i;
            
            if (!first) printf(",");
            if (start == end) printf("%d", start);
            else printf("%d-%d", start, end);
            first = 0;
        }
        i++;
    }
    printf(" ");
}

// affiche les details d'une tache a l'ecran
void afficher_tache(uint64_t task_id, const Timing *timing, const Command *cmd)
{
    printf("%lu: ", task_id);
    print_range(timing->minutes, 59);
    print_range(timing->heures, 23);
    print_range((uint64_t)timing->jours, 6);
    afficher_command_ligne(cmd);
    printf("\n");
}

// supprime un dossier et tout ce qu'il contient
void remove_recursive(const char *path) {
    DIR *d = opendir(path);
    if (!d) return;

    struct dirent *p;
    while ((p = readdir(d))) {
        if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) continue;

        char buf[512];
        snprintf(buf, sizeof(buf), "%s/%s", path, p->d_name);

        struct stat statbuf;
        if (stat(buf, &statbuf) == 0) {
            if (S_ISDIR(statbuf.st_mode)) {
                remove_recursive(buf);
            } else {
                unlink(buf);
            }
        }
    }
    closedir(d);
    rmdir(path);
}

// cree une nouvelle tache complexe en combinant des taches existantes
uint64_t creer_tache_complexe(const char *tasks_dir,Timing *t, uint16_t type_com, uint32_t nbtasks, uint64_t *ids) {
    // verifie d'abord que toutes les taches existent
    for (uint32_t i = 0; i < nbtasks; i++) {
        char path[256];
        snprintf(path, 256, "%s/%lu", tasks_dir, ids[i]);
        if (access(path, F_OK) != 0) {
            return 0; 
        }
    }

    // trouve un id libre
    uint64_t new_id = 0;
    while (1) {
        new_id++;
        char path[256];
        snprintf(path, 256, "%s/%lu", tasks_dir, new_id);
        if (access(path, F_OK) != 0) break;
    }

    char root[256], cmd_root[256];
    snprintf(root, 256, "%s/%lu", tasks_dir, new_id);
    snprintf(cmd_root, 256, "%s/%lu/cmd", tasks_dir, new_id);
    
    mkdir(root, 0777);
    mkdir(cmd_root, 0777);

    // creation du fichier timing
    char time_path[512];
    snprintf(time_path, sizeof(time_path), "%s/timing", root);
    int fd = open(time_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd != -1) {
        uint64_t m_be = htobe64(t->minutes);
        uint32_t h_be = htobe32(t->heures);
        write(fd, &m_be, 8);
        write(fd, &h_be, 4);
        write(fd, &t->jours, 1);
        close(fd);
    }

    // creation du fichier type
    char type_path[512];
    snprintf(type_path, sizeof(type_path), "%s/type", cmd_root);
    fd = open(type_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd != -1) {
        uint16_t type_be = htobe16(type_com);
        write(fd, &type_be, 2);
        close(fd);
    }

    // deplacement des dossiers cmd des anciennes taches vers la nouvelle
    for (uint32_t i = 0; i < nbtasks; i++) {
        char old_cmd_path[512];
        char new_cmd_sub[512];
        char old_root_path[512];

        snprintf(old_root_path, sizeof(old_root_path), "%s/%lu", tasks_dir, ids[i]);
        snprintf(old_cmd_path, sizeof(old_cmd_path), "%s/%lu/cmd", tasks_dir, ids[i]);
        snprintf(new_cmd_sub, sizeof(new_cmd_sub), "%s/%lu/cmd/%u", tasks_dir, new_id, i);

        rename(old_cmd_path, new_cmd_sub);

        // suppression des anciens dossiers racines
        remove_recursive(old_root_path);
    }

    return new_id;
}