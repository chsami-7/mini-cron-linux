#ifndef API_ERRAID_H
#define API_ERRAID_H

#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>

#include "tache.h"      
#include "io_utils.h"   

// declaration du tableau de taches et du compteur
extern Tache taches[2048];
extern int nb_taches;

// definitions des codes pour chaque operation
#define OP_LIST      0x4c53
#define OP_CREATE    0x4352
#define OP_COMBINE   0x4342
#define OP_STDOUT    0x534f
#define OP_STDERR    0x5345
#define OP_TIMES     0x5458
#define OP_TERMINATE 0x4b49 
#define OP_REMOVE    0x524d

// serialise toutes les taches et les envoie au client
void reponse_requete_list(int fd_reponse);

// lit et envoie le contenu du fichier de sortie standard ou d'erreur
void reponse_requete_output(int fd_req, int fd_rep, const char *tasks_dir, int is_stderr);

// lit et envoie l'historique d'execution de la tache
void reponse_requete_times(int fd_req, int fd_rep);

// gere la demande d'arret du demon
int reponse_requete_terminate(int fd_req, int fd_rep, const char *tasks_dir);

// gere la demande de suppression d'une tache
void reponse_requete_remove(int fd_req, int fd_rep);

// gere la creation d'une nouvelle tache simple
void reponse_requete_create(int fd_req, int fd_rep, const char *tasks_dir);

// lit l'opcode et appelle la fonction correspondante
int handle_request(int fd_req, const char *rep_pipe, const char *tasks_dir);

// recupere les arguments pour une combinaison de taches
int recevoir_details_combine(int fd, Timing *t, uint16_t *type_com, uint32_t *nbtasks, uint64_t **ids);

#endif