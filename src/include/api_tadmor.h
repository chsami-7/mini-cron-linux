#ifndef CLIENT_API_H
#define CLIENT_API_H

#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>

// Opcodes, Réponses, Erreurs
#define OP_LIST   0x4c53
#define OP_STDOUT 0x534f
#define OP_STDERR 0x5345
#define OP_TIMES  0x5458
#define ANS_OK 0x4f4b
#define ANS_ER 0x4552
#define ERR_NF 0x4e46
#define ERR_NR 0x4e52

// --- Fonctions d'Envoi de Requête ---

int envoie_requete_simple(int fd, uint16_t opcode);
int envoie_requete_id(int fd, uint16_t opcode, uint64_t taskid);

// --- Fonctions de Traitement de Réponse ---

void traiter_reponse_list(int fd_rep);
void traiter_reponse_output(int fd_rep);
void traiter_reponse_times(int fd_rep);

// --- Fonctions Utilitaires Client  ---

const char *lire_options_client(int argc, char *argv[], int *mode, uint64_t *taskid);
int construire_chemins_pipes(const char *pipes_dir, char req_path[256], char rep_path[256]);
int traiter_reponse_serveur(int fd_rep, int mode);

#endif