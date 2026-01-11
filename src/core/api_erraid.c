#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <endian.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <dirent.h>
#include <errno.h>

#include "api_erraid.h"
#include "command.h"    
#include "timing.h"     
#include "tache.h"      

// tableau externe contenant toutes les taches
extern Tache taches[2048];
extern int nb_taches;


// envoie la liste de toutes les taches au client
void reponse_requete_list(int fd_reponse)
{
    uint16_t anstype = htobe16(0x4f4b); // OK
    write_full(fd_reponse, &anstype, 2);

    uint32_t nb_tasks = htobe32(nb_taches);
    write_full(fd_reponse, &nb_tasks, 4);

    // boucle sur toutes les taches pour envoyer leurs infos
    for (int i = 0; i < nb_taches; i++)
    {
        uint64_t task_id = htobe64(taches[i].id);
        write_full(fd_reponse, &task_id, 8);
        serialise_timing(fd_reponse, &taches[i].timing);
        serialise_command(fd_reponse, &taches[i].cmd);
    }
}

// renvoie le contenu de la sortie standard ou d'erreur d'une tache
void reponse_requete_output(int fd_req, int fd_rep, const char *tasks_dir, int is_stderr)
{
    uint64_t taskid;
    if (read(fd_req, &taskid, 8) != 8)
        return;
    taskid = be64toh(taskid);

    char task_path[2048];
    snprintf(task_path, sizeof(task_path), "%s/%lu", tasks_dir, taskid);

    struct stat st;
    // verifie si le dossier de la tache existe
    if (stat(task_path, &st) != 0 || !S_ISDIR(st.st_mode))
    {
        send_error(fd_rep, 0x4e46); // NF
        return;
    }

    char file_path[2100];
    const char *fname = is_stderr ? "stderr" : "stdout";
    snprintf(file_path, sizeof(file_path), "%s/%s", task_path, fname);

    int fd = open(file_path, O_RDONLY);
    // si le fichier n'existe pas on renvoie une erreur
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
    // lecture du fichier et envoi au client
    while ((n = read(fd, buffer, sizeof(buffer))) > 0)
    {
        write_full(fd_rep, buffer, n);
    }
    close(fd);
}

// envoie l'historique des executions d'une tache
void reponse_requete_times(int fd_req, int fd_rep)
{
    uint64_t taskid;
    if (read(fd_req, &taskid, 8) != 8)
        return;
    taskid = be64toh(taskid);

    // cherche l'index de la tache
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

// fonction qui supprime tout le contenu d'un dossier
int supprimer_dossier_recursif(const char *path)
{
    DIR *dir = opendir(path);
    if (dir == NULL)
        return -1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode)) {
            supprimer_dossier_recursif(full_path);
        } else {
            unlink(full_path);
        }
    }
    closedir(dir);
    rmdir(path);
    return 0;
}

// gere la demande de suppression d'une tache
void reponse_requete_remove(int fd_req, int fd_rep)
{
    uint64_t taskid;
    if (read(fd_req, &taskid, 8) != 8)
        return;
    taskid = be64toh(taskid);

    int idx = -1;
    for (int i = 0; i < nb_taches; i++) {
        if ((uint64_t)taches[i].id == taskid) {
            idx = i;
            break;
        }
    }

    if (idx == -1) {
        send_error(fd_rep, 0x4e46); // NF 
        return;
    }

    // suppression des fichiers sur le disque
    if (supprimer_dossier_recursif(taches[idx].path) != 0) {
        send_error(fd_rep, 0x4552); // Erreur 
        return;
    }

    // on decale les taches suivantes dans le tableau
    for (int i = idx; i < nb_taches - 1; i++) {
        taches[i] = taches[i + 1];
    }
    nb_taches--;

    uint16_t ans = htobe16(0x4f4b);
    write_full(fd_rep, &ans, 2);
}

// lit une chaine envoyee sur le reseau avec sa taille
static char *lire_string_interne(int fd)
{
    uint32_t length;
    if (read(fd, &length, 4) != 4) return NULL;
    length = be32toh(length);
    char *str = malloc(length + 1);
    if (!str) return NULL;
    if (read(fd, str, length) != (ssize_t)length) { free(str); return NULL; }
    str[length] = '\0';
    return str;
}

// recupere les infos pour une combinaison de taches
int recevoir_details_combine(int fd, Timing *t, uint16_t *type_com, uint32_t *nbtasks, uint64_t **ids) {
    *t = deserialise_timing(fd);
    
    uint16_t type_be;
    if (read_full(fd, &type_be, 2) != 0) return -1;
    *type_com = be16toh(type_be);
    
    uint32_t nb_be;
    if (read_full(fd, &nb_be, 4) != 0) return -1;
    *nbtasks = be32toh(nb_be);
    
    *ids = malloc((*nbtasks) * sizeof(uint64_t));
    if (*ids == NULL) return -1;
    
    for (uint32_t i = 0; i < *nbtasks; i++) {
        uint64_t id_be;
        if (read_full(fd, &id_be, 8) != 0) {
            free(*ids);
            return -1;
        }
        (*ids)[i] = be64toh(id_be);
    }
    
    return 0;
}

static int index_tache_par_id(uint64_t id)
{
    for (int i = 0; i < nb_taches; i++) {
        if ((uint64_t)taches[i].id == id) return i;
    }
    return -1;
}

static void supprimer_tache_par_index(int idx)
{
    // supprime la tache du disque et decale le tableau
    supprimer_dossier_recursif(taches[idx].path);
    for (int i = idx; i < nb_taches - 1; i++) {
        taches[i] = taches[i + 1];
    }
    nb_taches--;
}

static void ajouter_tache_nouvelle(uint64_t new_id, const char *tasks_dir, Timing *t, Command *cmd)
{
    char task_path[2048];
    snprintf(task_path, sizeof(task_path), "%s/%lu", tasks_dir, new_id);
    taches[nb_taches].id = (int)new_id;
    taches[nb_taches].timing = *t;
    taches[nb_taches].cmd = *cmd;
    strncpy(taches[nb_taches].path, task_path, sizeof(taches[nb_taches].path) - 1);
    taches[nb_taches].path[sizeof(taches[nb_taches].path) - 1] = '\0';
    taches[nb_taches].derniere_execution = 0;
    nb_taches++;
}

static Command construire_command_depuis_cmd_dir(const char *cmd_dir)
{
    // recharge la commande depuis le disque
    return lire_command(cmd_dir);
}

// gere la creation d'une tache combinee complexe
static void reponse_requete_combine(int fd_req, int fd_rep, const char *tasks_dir)
{
    Timing t;
    uint16_t type_com;
    uint32_t nbtasks;
    uint64_t *ids = NULL;
    if (recevoir_details_combine(fd_req, &t, &type_com, &nbtasks, &ids) != 0) {
        send_error(fd_rep, 0x4552); // ER générique
        return;
    }

    // verifie que toutes les taches existent
    for (uint32_t i = 0; i < nbtasks; i++) {
        if (index_tache_par_id(ids[i]) < 0) {
            free(ids);
            send_error(fd_rep, 0x4e46); // NF
            return;
        }
    }

    // cree la structure de dossiers pour la nouvelle tache
    uint64_t new_id = creer_tache_complexe(tasks_dir,&t, type_com, nbtasks, ids);
    if (new_id == 0) {
        free(ids);
        send_error(fd_rep, 0x4552); // ER
        return;
    }

    // supprime les anciennes taches qui ont ete combinees
    for (uint32_t i = 0; i < nbtasks; i++) {
        for (int j = 0; j < nb_taches; j++) {
            if ((uint64_t)taches[j].id == ids[i]) { 
                supprimer_tache_par_index(j); break;
            }
        }
    }

    free(ids);

    // ajoute la nouvelle tache dans le tableau
    char cmd_dir[2048];
    snprintf(cmd_dir, sizeof(cmd_dir), "%s/%lu/cmd", tasks_dir, new_id);
    Command new_cmd = construire_command_depuis_cmd_dir(cmd_dir);
    ajouter_tache_nouvelle(new_id, tasks_dir, &t, &new_cmd);

    // repond ok au client
    uint16_t ans = htobe16(0x4f4b); // OK
    write_full(fd_rep, &ans, 2);
    uint64_t id_be = htobe64(new_id);
    write_full(fd_rep, &id_be, 8);
}

// gere la creation d'une tache simple
void reponse_requete_create(int fd_req, int fd_rep, const char *tasks_dir)
{
    Timing t = deserialise_timing(fd_req);
    
    Command c = {0};
    c.type = ('S' << 8 | 'I'); 
    
    // lit les arguments de la commande
    uint32_t argc_be;
    if (read(fd_req, &argc_be, 4) != 4) return;
    c.args.argc = be32toh(argc_be);
    c.args.argv = malloc(c.args.argc * sizeof(char*));
    
    for(uint32_t i=0; i<c.args.argc; i++) {
        c.args.argv[i] = lire_string_interne(fd_req);
    }

    // trouve un id libre
    int max_id = 0;
    for(int i=0; i<nb_taches; i++) {
        if (taches[i].id > max_id) max_id = taches[i].id;
    }
    int new_id = max_id + 1;

    char task_path[2048];
    snprintf(task_path, sizeof(task_path), "%s/%d", tasks_dir, new_id);
    mkdir(task_path, 0777);
    
    // ecrit le fichier timing
    char timing_path[2100];
    snprintf(timing_path, sizeof(timing_path), "%s/timing", task_path);
    int fdt = open(timing_path, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if(fdt != -1) {
        serialise_timing(fdt, &t);
        close(fdt);
    }
    
    char cmd_dir[2100];
    snprintf(cmd_dir, sizeof(cmd_dir), "%s/cmd", task_path);
    mkdir(cmd_dir, 0777);
    
    // ecrit le type de commande
    char type_path[2200]; 
    snprintf(type_path, sizeof(type_path), "%s/type", cmd_dir);
    int fdtp = open(type_path, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if(fdtp != -1) {
        uint16_t ty_be = htobe16(c.type);
        write(fdtp, &ty_be, 2);
        close(fdtp);
    }

    // ecrit les arguments
    char argv_path[2200];
    snprintf(argv_path, sizeof(argv_path), "%s/argv", cmd_dir);
    int fdav = open(argv_path, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if(fdav != -1) {
        uint32_t ac_be = htobe32(c.args.argc);
        write(fdav, &ac_be, 4);
        for(uint32_t i=0; i<c.args.argc; i++) {
            uint32_t l = strlen(c.args.argv[i]);
            uint32_t lb = htobe32(l);
            write(fdav, &lb, 4);
            write(fdav, c.args.argv[i], l);
        }
        close(fdav);
    }

    // mise a jour du tableau global
    taches[nb_taches].id = new_id;
    taches[nb_taches].timing = t;
    taches[nb_taches].cmd = c;
    strncpy(taches[nb_taches].path, task_path, sizeof(taches[nb_taches].path) - 1);
    taches[nb_taches].path[sizeof(taches[nb_taches].path) - 1] = '\0';
    taches[nb_taches].derniere_execution = 0;
    nb_taches++;

    uint16_t ans = htobe16(0x4f4b); // OK
    write_full(fd_rep, &ans, 2);
    uint64_t id_be = htobe64(new_id);
    write_full(fd_rep, &id_be, 8);
}

// fonction qui gere l'arret du serveur
int reponse_requete_terminate(int fd_req, int fd_rep, const char *tasks_dir)
{
    (void)fd_req;
    (void)tasks_dir;

    uint16_t ans = htobe16(0x4f4b); // OK
    write_full(fd_rep, &ans, 2);
    return 1;
}

// fonction principale qui recoit l'opcode et appelle la bonne fonction
int handle_request(int fd_req, const char *rep_pipe, const char *tasks_dir)
{
    uint16_t opcode;
    ssize_t lus = read(fd_req, &opcode, 2);

    if (lus != 2)
        return 0;

    int fd_rep = open(rep_pipe, O_WRONLY);
    if (fd_rep == -1)
        return 0;

    opcode = be16toh(opcode);
     int arret = 0;

    if (opcode == OP_LIST)
        reponse_requete_list(fd_rep);
    else if (opcode == OP_STDOUT)
        reponse_requete_output(fd_req, fd_rep, tasks_dir, 0);
    else if (opcode == OP_STDERR)
        reponse_requete_output(fd_req, fd_rep, tasks_dir, 1);
    else if (opcode == OP_TIMES)
        reponse_requete_times(fd_req, fd_rep);
    else if (opcode == OP_REMOVE)
            reponse_requete_remove(fd_req, fd_rep);
    else if (opcode == OP_CREATE)
            reponse_requete_create(fd_req, fd_rep, tasks_dir);
    else if (opcode == OP_COMBINE)
            reponse_requete_combine(fd_req, fd_rep, tasks_dir);
    else if (opcode ==OP_TERMINATE)
         arret =reponse_requete_terminate(fd_req, fd_rep, tasks_dir);

    close(fd_rep);
    return arret;
}