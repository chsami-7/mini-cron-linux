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
#include "timing.h"

int main(int argc, char *argv[]) {
    int mode = 0;
    /* Modes:
       1=List, 2=Stdout, 3=Stderr, 4=Times, 5=Term, 6=Remove, 7=Create, 8=Combine
    */
    
    uint64_t taskid = 0;
    const char *pipes_dir = NULL;
    
    // par defaut la tache s'execute tout le temps
    Timing t = { 0xFFFFFFFFFFFFFFFF, 0xFFFFFF, 0x7F };
    int is_null_timing = 0;
    uint16_t combine_type = 0;

    int opt;
    // analyse des options de la ligne de commande
    // P prend un argument pour le dossier des pipes
    // p n'en prend pas et sert pour le pipeline
    while ((opt = getopt(argc, argv, "P:plo:e:x:qr:cm:H:d:nsi")) != -1) {
        switch(opt) {
            case 'P': // P majuscule pour definir le dossier des tubes
                pipes_dir = optarg;
                break;
            
            case 'l': mode = 1; break; // lister les taches
            case 'o': mode = 2; taskid = strtoull(optarg, NULL, 10); break; // voir stdout
            case 'e': mode = 3; taskid = strtoull(optarg, NULL, 10); break; // voir stderr
            case 'x': mode = 4; taskid = strtoull(optarg, NULL, 10); break; // voir exitcodes
            case 'q': mode = 5; break; // tuer le demon
            case 'r': mode = 6; taskid = strtoull(optarg, NULL, 10); break; // supprimer une tache
            
            case 'c': mode = 7; break; // creer une tache
            case 's': // combiner en sequence
                mode = 8;
                combine_type = COM_SEQ; 
                break;
            case 'p': // combiner en pipeline
                mode = 8;
                combine_type = COM_pip;
                break;
            case 'i': // combiner avec un if
                mode = 8;
                combine_type = COM_IF;
                break;
            
            case 'n': is_null_timing = 1; break; // tache abstraite sans horaire
            
            // parsing des options de temps
            case 'm': t.minutes = parse_list_to_bitmap(optarg, 0, 59); break;
            case 'H': t.heures = (uint32_t)parse_list_to_bitmap(optarg, 0, 23); break;
            case 'd': t.jours = (uint8_t)parse_list_to_bitmap(optarg, 0, 6); break;
            
            default: fprintf(stderr, "Usage incorrect\n");
            return 1;
        }
    }

    // si option -n on met tous les bits a zero
    if (is_null_timing) {
        t.minutes = 0;
        t.heures = 0; t.jours = 0;
    }

    if (mode == 0) {
        fprintf(stderr, "Erreur: aucune commande spécifiée\n");
        return 1;
    }

    // construction des chemins vers les tubes nommes
    char req_path[256] = {0};
    char rep_path[256] = {0};
    if (construire_chemins_pipes(pipes_dir, req_path, rep_path) != 0) {
         fprintf(stderr, "Erreur: Problème lors de la construction des chemins de pipe.\n");
         return 1;
    }

    // ouverture du tube de requete en ecriture
    int fd_req = open(req_path, O_WRONLY);
    if (fd_req == -1) { perror("Erreur ouverture tube requête"); return 1; }

    // envoi de la requete selon le mode choisi
    if (mode == 7) {
        if (optind >= argc) { fprintf(stderr, "Erreur: commande manquante\n"); return 1; }
        envoie_requete_create(fd_req, &t, argc - optind, &argv[optind]);
    }
    else if (mode == 8) {
        if (optind >= argc) { fprintf(stderr, "Erreur: identifiants de tâches manquants\n"); return 1; }
        
        // recuperation des ids a combiner
        int nbtasks = argc - optind;
        uint64_t *ids = malloc(nbtasks * sizeof(uint64_t));
        if (!ids) { perror("malloc"); return 1; }
        
        for (int i = 0; i < nbtasks; i++) {
            ids[i] = strtoull(argv[optind+i], NULL, 10);
        }
        
        envoie_requete_combine(fd_req, &t, combine_type, nbtasks, ids);
        free(ids);
    }
    else if (mode == 1) envoie_requete_simple(fd_req, OP_LIST);
    else if (mode == 2) envoie_requete_id(fd_req, OP_STDOUT, taskid);
    else if (mode == 3) envoie_requete_id(fd_req, OP_STDERR, taskid);
    else if (mode == 4) envoie_requete_id(fd_req, OP_TIMES, taskid);
    else if (mode == 5) envoie_requete_simple(fd_req, OP_TERMINATE);
    else if (mode == 6) envoie_requete_id(fd_req, OP_REMOVE, taskid);
    
    close(fd_req);

    // ouverture du tube de reponse pour lire le resultat
    int fd_rep = open(rep_path, O_RDONLY);
    if (fd_rep == -1) { perror("Erreur ouverture tube réponse"); return 1; }
    
    // traitement et affichage de la reponse
    int result = traiter_reponse_serveur(fd_rep, mode); 
    
    close(fd_rep);
    return result;
}