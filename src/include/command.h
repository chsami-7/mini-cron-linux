#ifndef COMMAND_H
#define COMMAND_H

#include <stdint.h>
#include <time.h>
#include "timing.h"

/** Structure représentant les arguments d'une commande */
typedef struct
{
    char **argv;
    uint32_t argc;
} Arguments;

/** Structure représentant une commande */
typedef struct Command
{
    uint16_t type;
    Arguments args;
    uint32_t nbcmds;
    struct Command *cmds;
} Command;

/** Lit une commande depuis un répertoire */
Command lire_command(const char *cmd_dir);

/** Affiche une commande */
void afficher_command(const Command *cmd);

/** Libère la mémoire allouée pour une commande */
void liberer_command(Command *cmd);

/** Convertit un type de commande en sa représentation string */
const char *type_vers_string(uint16_t type);

/** Lit une chaîne de caractères depuis un descripteur de fichier */
char *lire_string(int fd);

/** Lit les arguments depuis un fichier argv */
Arguments lire_arguments(const char *argv_file);

/** Libère la mémoire allouée pour les arguments */
void liberer_arguments(Arguments *args);

#endif