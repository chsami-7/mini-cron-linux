#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <endian.h>
#include <signal.h> 
#include <string.h>
#include <stdlib.h> 
#include <time.h>

#include "command.h"

static int execute_recursive(const Command *cmd)
{
    if (cmd->type == ('S' << 8 | 'I'))
    {
        if (cmd->args.argc == 0) return 0;
        pid_t pid = fork();
        if (pid == -1) return -1;
        if (pid == 0) {
            char *argv[cmd->args.argc + 1];
            for (uint32_t i = 0; i < cmd->args.argc; i++) argv[i] = cmd->args.argv[i];
            argv[cmd->args.argc] = NULL;
            execvp(argv[0], argv);
            _exit(127);
        }
        int status;
        waitpid(pid, &status, 0);
        return status;
    }
    if (cmd->type == ('S' << 8 | 'Q')) {
        int last_status = 0;
        for (uint32_t i = 0; i < cmd->nbcmds; i++) {
            if (cmd->cmds[i].type == 0) continue;
            last_status = execute_recursive(&cmd->cmds[i]);
        }
        return last_status;
    }
    return -1;
}

int executer_tache(const char *task_dir, const Command *cmd)
{
    pid_t pid = fork();
    if (pid == -1) { perror("fork"); return -1; }

    if (pid == 0) {
        // le fils fait le travail
        signal(SIGCHLD, SIG_DFL); 
        
        // Capture l'heure de début pour le log
        time_t start_time = time(NULL);

        char outp[512], errp[512];
        snprintf(outp, sizeof(outp), "%s/stdout", task_dir);
        snprintf(errp, sizeof(errp), "%s/stderr", task_dir);

        int fd_out = open(outp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        int fd_err = open(errp, O_WRONLY | O_CREAT | O_TRUNC, 0644);

        if (fd_out != -1 && fd_err != -1) {
            dup2(fd_out, STDOUT_FILENO);
            dup2(fd_err, STDERR_FILENO);
            close(fd_out);
            close(fd_err);
        }

        int status = execute_recursive(cmd);

        uint16_t exitcode;
        if (WIFEXITED(status)) {
            int ret = WEXITSTATUS(status);
            exitcode = (ret == 255) ? 0xFFFF : (uint16_t)ret;
        } else {
            exitcode = 0xFFFF;
        }

        // écriture par le fils
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

        _exit(0); // Le fils s'arrête ici
    }
    
    return 0;
}

void print_range(uint64_t mask, int max_val) {
    if (mask == 0) { printf("- "); return; }
    uint64_t full_mask = 0;
    int limit = (max_val == 59) ? 60 : max_val + 1;
    for(int i=0; i < limit; i++) full_mask |= (1ULL << i);
    if ((mask & full_mask) == full_mask) { printf("* "); return; }
    int first = 1;
    int i = 0;
    while (i <= max_val) {
        if ((mask >> i) & 1) {
            int start = i;
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

void afficher_tache(uint64_t task_id, const Timing *timing, const Command *cmd)
{
    printf("%lu: ", task_id);
    print_range(timing->minutes, 59);
    print_range(timing->heures, 23);
    print_range((uint64_t)timing->jours, 6);
    if (strcmp(type_vers_string(cmd->type), "SI") == 0) {
        afficher_command_ligne(cmd);
    } else {
        for (uint32_t i = 0; i < cmd->nbcmds; i++) {
            if (i > 0) printf(" ; ");
            afficher_command_ligne(&cmd->cmds[i]);
        }
    }
    printf("\n");
}