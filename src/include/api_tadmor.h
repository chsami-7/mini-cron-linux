#ifndef API_TADMOR_H
#define API_TADMOR_H

#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include "timing.h"

// codes des operations possibles
#define OP_LIST      0x4c53 // 'LS'
#define OP_CREATE    0x4352 // 'CR'
#define OP_COMBINE   0x4342 // 'CB'
#define OP_REMOVE    0x524d // 'RM'
#define OP_TIMES     0x5458 // 'TX'
#define OP_STDOUT    0x534f // 'SO'
#define OP_STDERR    0x5345 // 'SE'
#define OP_TERMINATE 0x4b49 // 'TM'

// codes de reponse du serveur
#define ANS_OK 0x4f4b
#define ANS_ER 0x4552
#define ERR_NF 0x4e46
#define ERR_NR 0x4e52

// types de combinaisons de taches
#define COM_SEQ 0x5351
#define COM_pip 0x504c
#define COM_IF  0x4946

// --- Fonctions d'Envoi de Requête ---

// envoie une requete simple sans argument
int envoie_requete_simple(int fd, uint16_t opcode);

// envoie une requete avec un identifiant de tache
int envoie_requete_id(int fd, uint16_t opcode, uint64_t taskid);

// envoie une requete pour creer une tache simple
int envoie_requete_create(int fd, const Timing *t, int argc, char **argv);

// envoie une requete pour combiner plusieurs taches
int envoie_requete_combine(int fd, const Timing *t, uint16_t type_com, uint32_t nbtasks, uint64_t *taskids);

// --- Fonctions de Traitement de Réponse ---

// recoit et affiche la liste des taches
void traiter_reponse_list(int fd_rep);

// recoit et affiche la sortie standard ou d'erreur
void traiter_reponse_output(int fd_rep);

// recoit et affiche l'historique d'execution
void traiter_reponse_times(int fd_rep);

// recoit et affiche l'id de la tache creee
void traiter_reponse_creation(int fd_rep);

// --- Fonctions Utilitaires Client  ---

// construit les chemins complets vers les tubes nommes
int construire_chemins_pipes(const char *pipes_dir, char req_path[256], char rep_path[256]);

// lit la reponse du serveur et appelle la bonne fonction de traitement
int traiter_reponse_serveur(int fd_rep, int mode);

#endif