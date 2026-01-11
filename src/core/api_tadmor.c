#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <endian.h>
#include <sys/types.h>
#include <time.h> 

#include "api_tadmor.h" 
#include "io_utils.h"   
#include "timing.h"     
#include "command.h"    
#include "tache.h"      


// ----------------------------------------------------------------------
// Fonctions d'Envoi de Requête 
// ----------------------------------------------------------------------

// envoie une requete basique contenant juste le code de l'operation
int envoie_requete_simple(int fd, uint16_t opcode) {
    uint16_t op_be = htobe16(opcode);
    if (write(fd, &op_be, 2) != 2) return -1;
    return 0;
}

// envoie une requete qui cible une tache precise via son identifiant
int envoie_requete_id(int fd, uint16_t opcode, uint64_t taskid) {
    uint16_t op_be = htobe16(opcode);
    uint64_t id_be = htobe64(taskid);
    if (write(fd, &op_be, 2) != 2) return -1;
    if (write(fd, &id_be, 8) != 8) return -1;
    return 0;
}

// envoie toutes les infos pour creer une nouvelle tache simple
int envoie_requete_create(int fd, const Timing *t, int argc, char **argv)
{
    uint16_t op_be = htobe16(OP_CREATE);
    if (write(fd, &op_be, 2) != 2) return -1;
    
    // envoi du timing
    serialise_timing(fd, t);
    
    // envoi du nombre d'arguments
    uint32_t ac_be = htobe32(argc);
    write(fd, &ac_be, 4);
    
    // envoi des arguments un par un avec leur taille
    for (int i = 0; i < argc; i++) {
        uint32_t len = strlen(argv[i]);
        uint32_t len_be = htobe32(len);
        write(fd, &len_be, 4);
        write(fd, argv[i], len);
    }
    return 0;
}

// envoie les infos pour combiner plusieurs taches existantes
int envoie_requete_combine(int fd, const Timing *t, uint16_t type_com, uint32_t nbtasks, uint64_t *taskids)
{
    uint16_t op_be = htobe16(OP_COMBINE);
    if (write(fd, &op_be, 2) != 2) return -1;

    serialise_timing(fd, t);

    // envoi du type de combinaison sequence ou pipe
    uint16_t type_be = htobe16(type_com);
    if (write(fd, &type_be, 2) != 2) return -1;

    // envoi du nombre de taches a combiner
    uint32_t nb_be = htobe32(nbtasks);
    if (write(fd, &nb_be, 4) != 4) return -1;

    // envoi de la liste des ids
    for (uint32_t i = 0; i < nbtasks; i++) {
        uint64_t id_be = htobe64(taskids[i]);
        if (write(fd, &id_be, 8) != 8) return -1;
    }

    return 0;
}

// ----------------------------------------------------------------------
// Fonctions de Traitement de Réponse 
// ----------------------------------------------------------------------

// lit et affiche la liste des taches renvoyee par le serveur
void traiter_reponse_list(int fd_rep) {
    uint32_t nbtaches;
    if (read_full(fd_rep, &nbtaches, 4) != 0) {
        fprintf(stderr, "Erreur lecture nombre de tâches\n");
        return;
    }
    nbtaches = be32toh(nbtaches);

    // boucle pour lire chaque tache et l'afficher
    for (uint32_t i = 0; i < nbtaches; i++) {
        uint64_t task_id;
        if (read_full(fd_rep, &task_id, 8) != 0) break;
        task_id = be64toh(task_id);

        Timing timing = deserialise_timing(fd_rep);
        Command command = deserialise_command(fd_rep);

        afficher_tache(task_id, &timing, &command); 

        liberer_command(&command);
    }
}

// recupere le contenu d'un fichier de sortie et l'affiche
void traiter_reponse_output(int fd_rep) {
    uint32_t len; 
    if (read_full(fd_rep, &len, 4) != 0) return;
    len = be32toh(len);

    char buffer[1024];
    uint32_t remaining = len;
    
    // lecture par blocs pour ne pas saturer la memoire
    while (remaining > 0) {
        int to_read = (remaining < sizeof(buffer)) ? remaining : sizeof(buffer);
        if (read_full(fd_rep, buffer, to_read) != 0) break;
        write(STDOUT_FILENO, buffer, to_read);
        remaining -= to_read;
    }
}

// affiche l'historique d'execution avec les dates et codes de retour
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

        // formatage de la date pour l'affichage
        struct tm tm;
        localtime_r(&t, &tm);
        char buf[128];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        printf("%s %u\n", buf, code);
    }
}

// affiche simplement l'id de la tache qui vient d'etre creee
void traiter_reponse_creation(int fd_rep) {
    uint64_t tid;
    if (read_full(fd_rep, &tid, 8) == 0) {
        printf("%lu\n", be64toh(tid));
    }
}

// ----------------------------------------------------------------------
// Fonctions Utilitaires Client  
// ----------------------------------------------------------------------

// genere les chemins des tubes nommes
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
    
    if (req_path[255] != '\0' || rep_path[255] != '\0') return -1;
    return 0;
}

// verifie si le serveur a repondu ok ou erreur et lance l'affichage adapte
int traiter_reponse_serveur(int fd_rep, int mode)
{
    uint16_t ans_type;
    
    if (read_full(fd_rep, &ans_type, 2) != 0) {
        fprintf(stderr, "Erreur lecture réponse\n");
        return 1;
    }
    ans_type = be16toh(ans_type);

    // gestion des erreurs renvoyees par le serveur
    if (ans_type == ANS_ER) {
        uint16_t err_code;
        read_full(fd_rep, &err_code, 2);
        err_code = be16toh(err_code);
        
        if (err_code == ERR_NF) fprintf(stderr, "Erreur: Tâche non trouvée\n");
        else if (err_code == ERR_NR) fprintf(stderr, "Erreur: Tâche jamais exécutée\n");
        else fprintf(stderr, "Erreur inconnue: 0x%x\n", err_code);
        
        return 1; 
    } 
    // si c'est ok on appelle la fonction correspondant a la commande
    else if (ans_type == ANS_OK) {
        if (mode == 1) traiter_reponse_list(fd_rep);
        else if (mode == 4) traiter_reponse_times(fd_rep);
        else if (mode == 7 || mode == 8) traiter_reponse_creation(fd_rep);
        else if (mode == 5 || mode==6) return 0; // Terminate / Remove
        else traiter_reponse_output(fd_rep); // Stdout / Stderr
        
        return 0; 
    } 
    else {
        fprintf(stderr, "Type de réponse inconnu: 0x%x\n", ans_type);
        return 1;
    }
}