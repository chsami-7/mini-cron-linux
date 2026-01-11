#ifndef COMMAND_H
#define COMMAND_H

#include <stdint.h>
#include <time.h>
#include "timing.h"

// structure pour stocker la liste des arguments d'un programme
typedef struct
{
    char **argv;
    uint32_t argc;
} Arguments;

// structure principale qui represente une commande simple ou complexe
typedef struct Command
{
    uint16_t type; // type de commande SI SQ PL ou IF
    Arguments args; // utilise seulement si c'est une commande simple
    uint32_t nbcmds; // nombre de sous-commandes
    struct Command *cmds; // tableau dynamique des sous-commandes
} Command;

// charge une structure commande depuis un dossier sur le disque
Command lire_command(const char *cmd_dir);

// affiche le contenu detaille de la commande pour le debug
void afficher_command(const Command *cmd);

// libere toute la memoire utilisee par la commande et ses sous-commandes
void liberer_command(Command *cmd);

// renvoie le nom du type en chaine de caracteres comme SI ou PL
const char *type_vers_string(uint16_t type);

// lit une chaine precedee de sa taille dans un descripteur
char *lire_string(int fd);

// charge les arguments depuis un fichier argv
Arguments lire_arguments(const char *argv_file);

// libere la memoire du tableau d'arguments
void liberer_arguments(Arguments *args);

// ecrit la commande en binaire sur le reseau
int serialise_command(int fd, const Command *cmd);

// lit la commande en binaire depuis le reseau
Command deserialise_command(int fd);

// affiche la commande sous une forme lisible comme un shell
void afficher_command_ligne(const Command *cmd);

#endif