#ifndef API_ERRAID_H
#define API_ERRAID_H

#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>

#include "tache.h"      // Contient la structure Tache
#include "io_utils.h"   // Pour send_error, write_full

// Déclaration du tableau de tâches et du compteur
extern Tache taches[2048];
extern int nb_taches;

// Définitions des Opcodes
#define OP_LIST   0x4c53
#define OP_STDOUT 0x534f
#define OP_STDERR 0x5345
#define OP_TIMES  0x5458

/** Réponse à LIST: sérialise toutes les tâches et les envoie. */
void reponse_requete_list(int fd_reponse);

/** Réponse à STDOUT / STDERR: lit et envoie le contenu du fichier d'output de la tâche. */
void reponse_requete_output(int fd_req, int fd_rep, const char *tasks_dir, int is_stderr);

/** Réponse à TIMES (TX): lit et envoie les logs d'exécution de la tâche. */
void reponse_requete_times(int fd_req, int fd_rep);

/** Gère le décodage de l'opcode et dispatche la requête. */
void handle_request(int fd_req, const char *rep_pipe, const char *tasks_dir);

#endif