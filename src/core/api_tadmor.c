#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <endian.h>
#include <time.h> 

#include "api_tadmor.h" // En-tête du module
#include "io_utils.h"   // Pour read_full, write_full
#include "timing.h"     // Pour deserialise_timing
#include "command.h"    // Pour deserialise_command, liberer_command
#include "tache.h"      // Pour afficher_tache


// ----------------------------------------------------------------------
// Fonctions d'Envoi de Requête 
// ----------------------------------------------------------------------

int envoie_requete_simple(int fd, uint16_t opcode) {
    uint16_t op_be = htobe16(opcode);
    if (write(fd, &op_be, 2) != 2) return -1;
    return 0;
}

int envoie_requete_id(int fd, uint16_t opcode, uint64_t taskid) {
    uint16_t op_be = htobe16(opcode);
    uint64_t id_be = htobe64(taskid);
    if (write(fd, &op_be, 2) != 2) return -1;
    if (write(fd, &id_be, 8) != 8) return -1;
    return 0;
}

// ----------------------------------------------------------------------
// Fonctions de Traitement de Réponse 
// ----------------------------------------------------------------------

void traiter_reponse_list(int fd_rep) {
    uint32_t nbtaches;
    if (read_full(fd_rep, &nbtaches, 4) != 0) {
        fprintf(stderr, "Erreur lecture nombre de tâches\n");
        return;
    }
    nbtaches = be32toh(nbtaches);

    for (uint32_t i = 0; i < nbtaches; i++) {
        uint64_t task_id;
        if (read_full(fd_rep, &task_id, 8) != 0) break;
        task_id = be64toh(task_id);

        // Désérialisation des structures
        Timing timing = deserialise_timing(fd_rep);
        Command command = deserialise_command(fd_rep);

        // Affichage
        afficher_tache(task_id, &timing, &command); 
        printf("\n");

        // Libération de la mémoire
        liberer_command(&command);
    }
}

void traiter_reponse_output(int fd_rep) {
    uint32_t len; 
    if (read_full(fd_rep, &len, 4) != 0) return;
    len = be32toh(len);

    char buffer[1024];
    uint32_t remaining = len;
    
    // Boucle de lecture/écriture vers STDOUT
    while (remaining > 0) {
        int to_read = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);
        if (read_full(fd_rep, buffer, to_read) != 0) break;
        write(STDOUT_FILENO, buffer, to_read);
        remaining -= to_read;
    }
}

void traiter_reponse_times(int fd_rep) {
    uint32_t nbruns;
    if (read_full(fd_rep, &nbruns, 4) != 0) {
        fprintf(stderr, "Erreur lecture nombre de runs\n");
        return;
    }
    nbruns = be32toh(nbruns);

    for (uint32_t i = 0; i < nbruns; i++) {
        int64_t t_be;
        uint16_t code_be;
        if (read_full(fd_rep, &t_be, 8) != 0) break;
        if (read_full(fd_rep, &code_be, 2) != 0) break;
        
        time_t t = (time_t) be64toh(t_be);
        uint16_t code = be16toh(code_be);

        struct tm tm;
        localtime_r(&t, &tm);
        char buf[128];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        printf("%s %u\n", buf, code);
    }
}

// ----------------------------------------------------------------------
// Fonctions Utilitaires Client 
// ----------------------------------------------------------------------

const char *lire_options_client(int argc, char *argv[], int *mode, uint64_t *taskid) 
{
    const char *pipes_dir = NULL;
    int c;

    optind = 1; 
    *mode = 0;
    *taskid = 0;
    
    while ((c = getopt(argc, argv, "p:lo:e:x:")) != -1) {
        switch (c) {
            case 'p': 
                pipes_dir = optarg;
                break;
            case 'l': 
                *mode = 1; 
                break;
            case 'o': 
                *mode = 2; 
                *taskid = strtoull(optarg, NULL, 10); 
                break;
            case 'e': 
                *mode = 3; 
                *taskid = strtoull(optarg, NULL, 10); 
                break;
            case 'x': 
                *mode = 4; 
                *taskid = strtoull(optarg, NULL, 10); 
                break;
            default: 
                return NULL; // Erreur d'usage
        }
    }
    
    if (*mode == 0) return NULL; 
    return pipes_dir;
}

int construire_chemins_pipes(const char *pipes_dir, char req_path[256], char rep_path[256])
{
    if (pipes_dir) {
        snprintf(req_path, 256, "%s/erraid-request-pipe", pipes_dir);
        snprintf(rep_path, 256, "%s/erraid-reply-pipe", pipes_dir);
    } else {
        const char *user = getenv("USER");
        if (!user) user = "default";
        
        snprintf(req_path, 256, "/tmp/%s/erraid/pipes/erraid-request-pipe", user);
        snprintf(rep_path, 256, "/tmp/%s/erraid/pipes/erraid-reply-pipe", user);
    }
    
    // Vérification de base
    if (req_path[255] != '\0' || rep_path[255] != '\0') return -1;
    return 0;
}

int traiter_reponse_serveur(int fd_rep, int mode)
{
    uint16_t ans_type;
    
    if (read_full(fd_rep, &ans_type, 2) != 0) {
        fprintf(stderr, "Erreur lecture réponse\n");
        return 1;
    }
    ans_type = be16toh(ans_type);

    if (ans_type == ANS_ER) {
        uint16_t err_code;
        read_full(fd_rep, &err_code, 2);
        err_code = be16toh(err_code);
        
        if (err_code == ERR_NF) fprintf(stderr, "Erreur: Tâche non trouvée\n");
        else if (err_code == ERR_NR) fprintf(stderr, "Erreur: Tâche jamais exécutée\n");
        else fprintf(stderr, "Erreur inconnue: 0x%x\n", err_code);
        
        return 1; 
    } 
    else if (ans_type == ANS_OK) {
        if (mode == 1) traiter_reponse_list(fd_rep);
        else if (mode == 4) traiter_reponse_times(fd_rep);
        else traiter_reponse_output(fd_rep);
        
        return 0; 
    } 
    else {
        fprintf(stderr, "Type de réponse inconnu: 0x%x\n", ans_type);
        return 1;
    }
}