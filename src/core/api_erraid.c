#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <endian.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "api_erraid.h"
#include "command.h"    // Fonctions serialise_command, deserialise_command
#include "timing.h"     // Fonction serialise_timing, deserialise_timing

// Le tableau de tâches et le compteur sont externes
extern Tache taches[2048];
extern int nb_taches;


void reponse_requete_list(int fd_reponse)
{
    uint16_t anstype = htobe16(0x4f4b); // OK
    write_full(fd_reponse, &anstype, 2);

    uint32_t nb_tasks = htobe32(nb_taches);
    write_full(fd_reponse, &nb_tasks, 4);

    for (int i = 0; i < nb_taches; i++)
    {
        uint64_t task_id = htobe64(taches[i].id);
        write_full(fd_reponse, &task_id, 8);
        serialise_timing(fd_reponse, &taches[i].timing);
        serialise_command(fd_reponse, &taches[i].cmd);
    }
}

void reponse_requete_output(int fd_req, int fd_rep, const char *tasks_dir, int is_stderr)
{
    uint64_t taskid;
    if (read(fd_req, &taskid, 8) != 8)
        return;
    taskid = be64toh(taskid);

    char task_path[2048];
    snprintf(task_path, sizeof(task_path), "%s/%lu", tasks_dir, taskid);

    struct stat st;
    if (stat(task_path, &st) != 0 || !S_ISDIR(st.st_mode))
    {
        send_error(fd_rep, 0x4e46); // NF
        return;
    }

    char file_path[2100];
    const char *fname = is_stderr ? "stderr" : "stdout";
    snprintf(file_path, sizeof(file_path), "%s/%s", task_path, fname);

    int fd = open(file_path, O_RDONLY);
    if (fd == -1)
    {
        send_error(fd_rep, 0x4e52); // NR
        return;
    }
    if (fstat(fd, &st) != 0)
    {
        close(fd);
        send_error(fd_rep, 0x4e52);
        return;
    }

    uint16_t ans = htobe16(0x4f4b); // OK
    write_full(fd_rep, &ans, 2);

    uint32_t len = (uint32_t)st.st_size;
    uint32_t len_be = htobe32(len);
    write_full(fd_rep, &len_be, 4);

    char buffer[1024];
    ssize_t n;
    while ((n = read(fd, buffer, sizeof(buffer))) > 0)
    {
        write_full(fd_rep, buffer, n);
    }
    close(fd);
}


void reponse_requete_times(int fd_req, int fd_rep)
{
    uint64_t taskid;
    if (read(fd_req, &taskid, 8) != 8)
        return;
    taskid = be64toh(taskid);

    int idx = -1;
    for (int i = 0; i < nb_taches; i++)
    {
        if ((uint64_t)taches[i].id == taskid)
        {
            idx = i;
            break;
        }
    }

    if (idx == -1)
    {
        send_error(fd_rep, 0x4e46); // NF
        return;
    }

    char times_path[2100];
    snprintf(times_path, sizeof(times_path), "%s/times-exitcodes", taches[idx].path);

    int fd_times = open(times_path, O_RDONLY);
    uint32_t nbruns = 0;
    if (fd_times != -1)
    {
        struct stat st;
        if (fstat(fd_times, &st) == 0)
        {
            nbruns = st.st_size / 10;
        }
    }

    uint16_t ans = htobe16(0x4f4b); // OK
    write_full(fd_rep, &ans, 2);

    uint32_t nbruns_be = htobe32(nbruns);
    write_full(fd_rep, &nbruns_be, 4);

    if (fd_times != -1)
    {
        char buf[1024];
        ssize_t n;
        while ((n = read(fd_times, buf, sizeof(buf))) > 0)
        {
            write_full(fd_rep, buf, n);
        }
        close(fd_times);
    }
}

void handle_request(int fd_req, const char *rep_pipe, const char *tasks_dir)
{
    uint16_t opcode;
    ssize_t lus = read(fd_req, &opcode, 2);

    if (lus != 2)
        return;

    int fd_rep = open(rep_pipe, O_WRONLY);
    if (fd_rep == -1)
        return;

    opcode = be16toh(opcode);

    if (opcode == OP_LIST)
        reponse_requete_list(fd_rep);
    else if (opcode == OP_STDOUT)
        reponse_requete_output(fd_req, fd_rep, tasks_dir, 0);
    else if (opcode == OP_STDERR)
        reponse_requete_output(fd_req, fd_rep, tasks_dir, 1);
    else if (opcode == OP_TIMES)
        reponse_requete_times(fd_req, fd_rep);

    close(fd_rep);
}