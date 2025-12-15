#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <time.h> 

#include "api_tadmor.h" 

int main(int argc, char *argv[]) {
    // Variables du main
    int mode = 0;
    uint64_t taskid = 0;
    char req_path[256], rep_path[256];
    
    // 1. Lire et valider les options
    const char *pipes_dir = lire_options_client(argc, argv, &mode, &taskid);

    if (pipes_dir == NULL) {
        fprintf(stderr, "Usage: %s [-p pipes_dir] [-l | -o ID | -e ID | -x ID]\n", argv[0]);
        return 1;
    }

    // 2. Construire les chemins des pipes
    if (construire_chemins_pipes(pipes_dir, req_path, rep_path) != 0) {
         fprintf(stderr, "Erreur: Problème lors de la construction des chemins de pipe.\n");
         return 1;
    }

    // 3. Envoyer la requête
    int fd_req = open(req_path, O_WRONLY);
    if (fd_req == -1) { perror("Erreur ouverture tube requête"); return 1; }

    if (mode == 1) envoie_requete_simple(fd_req, OP_LIST);
    else if (mode == 2) envoie_requete_id(fd_req, OP_STDOUT, taskid);
    else if (mode == 3) envoie_requete_id(fd_req, OP_STDERR, taskid);
    else if (mode == 4) envoie_requete_id(fd_req, OP_TIMES, taskid);
    
    close(fd_req); 

    // 4. Recevoir la réponse et la traiter
    int fd_rep = open(rep_path, O_RDONLY);
    if (fd_rep == -1) { perror("Erreur ouverture tube réponse"); return 1; }
    
    int result = traiter_reponse_serveur(fd_rep, mode); 
    
    close(fd_rep);
    return result;
}