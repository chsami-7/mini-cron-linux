#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <endian.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>

#include "command.h"

// definitions des types de commandes en binaire
#define CMD_SI ('S' << 8 | 'I')
#define CMD_SQ ('S' << 8 | 'Q')
#define CMD_PL ('P' << 8 | 'L')
#define CMD_IF ('I' << 8 | 'F')

static const char *TYPES_COMMANDES[] = {"SI", "SQ", "PL", "IF"};
static const int NB_TYPES = 4;

// convertit le code binaire du type en chaine lisible
const char *type_vers_string(uint16_t type)
{
    char type_str[3];
    type_str[0] = (type >> 8) & 0xFF;
    type_str[1] = type & 0xFF;
    type_str[2] = '\0';

    for (int i = 0; i < NB_TYPES; i++)
    {
        if (strcmp(type_str, TYPES_COMMANDES[i]) == 0)
        {
            return TYPES_COMMANDES[i];
        }
    }
    return "??";
}

// lit une chaine de caracteres precedee de sa taille dans le fichier
char *lire_string(int fd)
{
    uint32_t length;
    if (read(fd, &length, 4) != 4)
    {
        return NULL;
    }
    length = be32toh(length);

    char *str = malloc(length + 1);
    if (!str)
    {
        return NULL;
    }

    if (read(fd, str, length) != (ssize_t)length)
    {
        free(str);
        return NULL;
    }
    str[length] = '\0';
    return str;
}

// lit le fichier argv qui contient tous les arguments
Arguments lire_arguments(const char *argv_file)
{
    Arguments args = {NULL, 0};

    int fd = open(argv_file, O_RDONLY);
    if (fd == -1)
    {
        return args;
    }

    uint32_t argc;
    // lecture du nombre d'arguments
    if (read(fd, &argc, 4) != 4)
    {
        close(fd);
        return args;
    }
    argc = be32toh(argc);

    args.argv = malloc(argc * sizeof(char *));
    if (!args.argv)
    {
        close(fd);
        return args;
    }

    args.argc = argc;
    // lecture de chaque argument un par un
    for (uint32_t i = 0; i < argc; i++)
    {
        args.argv[i] = lire_string(fd);
        if (!args.argv[i])
        {
            for (uint32_t j = 0; j < i; j++)
            {
                free(args.argv[j]);
            }
            free(args.argv);
            args.argv = NULL;
            args.argc = 0;
            break;
        }
    }

    close(fd);
    return args;
}

// lit le type de commande depuis le fichier type du dossier
static uint16_t lire_type_commande(const char *cmd_dir)
{
    char type_path[512];
    snprintf(type_path, sizeof(type_path), "%s/type", cmd_dir);

    int fd = open(type_path, O_RDONLY);
    if (fd == -1) return 0;

    uint16_t type_raw;
    if (read(fd, &type_raw, 2) != 2) {
        close(fd);
        return 0;
    }
    close(fd);

    return be16toh(type_raw);
}

// charge une commande simple en lisant ses arguments
static Command lire_commande_simple(const char *cmd_dir)
{
    Command cmd = {0};
    cmd.type = CMD_SI;
    char argv_path[512];
    snprintf(argv_path, sizeof(argv_path), "%s/argv", cmd_dir);
    cmd.args = lire_arguments(argv_path);
    return cmd;
}

// charge une commande complexe en lisant les sous-dossiers
static Command lire_commande_composee(const char *cmd_dir, uint16_t type)
{
    Command cmd = {0};
    cmd.type = type;
    cmd.args.argc = 0;
    cmd.args.argv = NULL;

    DIR *dir = opendir(cmd_dir);
    if (!dir) return cmd;

    struct dirent *entry;
    cmd.nbcmds = 0;

    // compte le nombre de sous-commandes
    while ((entry = readdir(dir)) != NULL) {
        if (isdigit(entry->d_name[0])) cmd.nbcmds++;
    }
    closedir(dir);

    if (cmd.nbcmds == 0) return cmd;

    cmd.cmds = malloc(cmd.nbcmds * sizeof(Command));
    if (!cmd.cmds) { cmd.nbcmds = 0; return cmd; }

    // lit chaque sous-commande recursivement
    for (uint32_t i = 0; i < cmd.nbcmds; i++) {
        char subcmd_path[512];
        snprintf(subcmd_path, sizeof(subcmd_path), "%s/%u", cmd_dir, i);
        cmd.cmds[i] = lire_command(subcmd_path);
    }
    return cmd;
}

// fonction principale pour charger une commande depuis le disque
Command lire_command(const char *cmd_dir)
{
    uint16_t type = lire_type_commande(cmd_dir);
    if (type == 0) return (Command){0};

    if (type == CMD_SI)
        return lire_commande_simple(cmd_dir);
    else
        return lire_commande_composee(cmd_dir, type);
}


// envoie la structure de la commande sur le reseau
int serialise_command(int fd, const Command *cmd)
{
    if (!cmd)
    {
        fprintf(stderr, "Erreur: command null\n");
        return -1;
    }
    if (cmd->type == 0)
    {
        fprintf(stderr, "Erreur: command type 0 (non initialisée)\n");
        return -1;
    }
    const char *type_str = type_vers_string(cmd->type);

    // cas commande simple
    if (strcmp(type_str, "SI") == 0)
    {
        uint16_t type = htobe16(cmd->type);
        uint32_t argc = htobe32(cmd->args.argc);

        write(fd, &type, 2);
        write(fd, &argc, 4);

        for (uint32_t i = 0; i < cmd->args.argc; i++)
        {
            uint32_t len = htobe32(strlen(cmd->args.argv[i]));
            write(fd, &len, 4);
            write(fd, cmd->args.argv[i], strlen(cmd->args.argv[i]));
        }
    }
    // cas commande composee
    else
    {
        uint16_t type = htobe16(cmd->type);
        uint32_t nbcmds = htobe32(cmd->nbcmds);

        write(fd, &type, 2);
        write(fd, &nbcmds, 4);
        if (cmd->type == CMD_SQ )
        {
            if (cmd->nbcmds == 0 || !cmd->cmds)
            {
                fprintf(stderr, "Erreur: SQ sans sous-commandes\n");
                return -1;
            }
        }
        // appel recursif pour envoyer les sous-commandes
        for (uint32_t i = 0; i < cmd->nbcmds; i++)
        {
            if (cmd->cmds[i].type == 0)
            {
                fprintf(stderr, "Erreur: sous-commande %u non initialisée\n", i);
                continue;
            }
            serialise_command(fd, &cmd->cmds[i]);
        }
    }
    return 0;
}


// reconstruit une commande a partir des donnees du reseau
Command deserialise_command(int fd)
{
    Command cmd = {0};
    uint16_t type_raw;
    if (read(fd, &type_raw, 2) != 2)
    {
        return cmd;
    }
    cmd.type = be16toh(type_raw);

    const char *type_str = type_vers_string(cmd.type);

    // lecture commande simple
    if (strcmp(type_str, "SI") == 0)
    {
        uint32_t argc_raw;
        if (read(fd, &argc_raw, 4) != 4)
        {
            return cmd;
        }
        cmd.args.argc = be32toh(argc_raw);
        cmd.args.argv = malloc(cmd.args.argc * sizeof(char *));
        if (!cmd.args.argv && cmd.args.argc > 0)
        {
            cmd.args.argc = 0;
            return cmd;
        }
        for (uint32_t i = 0; i < cmd.args.argc; i++)
        {
            cmd.args.argv[i] = lire_string(fd);
            if (!cmd.args.argv[i])
            {
                for (uint32_t j = 0; j < i; j++)
                    free(cmd.args.argv[j]);
                free(cmd.args.argv);
                cmd.args.argv = NULL;
                cmd.args.argc = 0;
                break;
            }
        }
        cmd.nbcmds = 0;
        cmd.cmds = NULL;
    }
    // lecture commande composee
    else
    {
        uint32_t nbcmds_raw;
        if (read(fd, &nbcmds_raw, 4) != 4)
        {
            return cmd;
        }
        cmd.nbcmds = be32toh(nbcmds_raw);
        cmd.cmds = malloc(cmd.nbcmds * sizeof(Command));
        if (!cmd.cmds && cmd.nbcmds > 0)
        {
            cmd.nbcmds = 0;
            return cmd;
        }
        // appel recursif pour lire les sous-commandes
        for (uint32_t i = 0; i < cmd.nbcmds; i++)
        {
            cmd.cmds[i] = deserialise_command(fd);
        }
        cmd.args.argc = 0;
        cmd.args.argv = NULL;
    }
    return cmd;
}

// affichage detaille pour le debug
void afficher_command(const Command *cmd)
{
    printf("------affichage de la commande-------\n");
    printf("Type: %s\n", type_vers_string(cmd->type));

    if (strcmp(type_vers_string(cmd->type), "SI") == 0)
    {

        if (cmd->args.argc > 0)
        {
            printf("Nombre d'arguments: %u\n", cmd->args.argc);
            for (uint32_t i = 0; i < cmd->args.argc; i++)
            {
                printf("Arg[%u]: \"%s\"\n", i, cmd->args.argv[i]);
            }

            printf("Commande complète: ");
            for (uint32_t i = 0; i < cmd->args.argc; i++)
            {
                if (i > 0)
                {
                    printf(" ");
                }
                if (strchr(cmd->args.argv[i], ' '))
                {
                    printf("'%s'", cmd->args.argv[i]);
                }
                else
                {
                    printf("%s", cmd->args.argv[i]);
                }
            }
            printf("\n");
        }
    }
    else
    {

        printf("Nombre de sous-commandes: %u\n", cmd->nbcmds);
        for (uint32_t i = 0; i < cmd->nbcmds; i++)
        {
            printf("\n=== Sous-commande %u ===\n", i);
            afficher_command(&cmd->cmds[i]);
        }
    }
}

// fonction recursive pour afficher la commande joliment
static void afficher_command_recursif(const Command *cmd, int is_root) {
    const char *type = type_vers_string(cmd->type);

    if (strcmp(type, "SI") == 0) {
        for (uint32_t i = 0; i < cmd->args.argc; i++) {
            if (i > 0) printf(" ");
            printf("%s", cmd->args.argv[i]);
        }
    }
    else if (strcmp(type, "SQ") == 0 || strcmp(type, "PL") == 0) {
        if (!is_root) printf("( ");
        
        for (uint32_t i = 0; i < cmd->nbcmds; i++) {
            if (i > 0) {
                if (strcmp(type, "SQ") == 0) printf(" ; ");
                else printf(" | ");
            }
            afficher_command_recursif(&cmd->cmds[i], 0);
        }
        
        if (!is_root) printf(" )");
    }
    else if (strcmp(type, "IF") == 0) {
        if (!is_root) printf("( ");
        
        printf("if ");
        if (cmd->nbcmds >= 1) {
            afficher_command_recursif(&cmd->cmds[0], 0);
            if (cmd->cmds[0].type == CMD_SI) printf(" ;");
        }
        
        printf(" then ");
        if (cmd->nbcmds >= 2) {
            afficher_command_recursif(&cmd->cmds[1], 0);
            if (cmd->cmds[1].type == CMD_SI) printf(" ;");
        }
        
        if (cmd->nbcmds >= 3) {
            printf(" else ");
            afficher_command_recursif(&cmd->cmds[2], 0);
            if (cmd->cmds[2].type == CMD_SI) printf(" ;");
        }
        
        printf(" fi");
        
        if (!is_root) printf(" )");
    }
}

// point d'entree pour l'affichage propre
void afficher_command_ligne(const Command *cmd)
{
    afficher_command_recursif(cmd, 1);
}

// libere toute la memoire utilisee par les arguments
void liberer_arguments(Arguments *args)
{
    if (args->argv)
    {
        for (uint32_t i = 0; i < args->argc; i++)
        {
            free(args->argv[i]);
        }
        free(args->argv);
        args->argv = NULL;
    }
    args->argc = 0;
}

// libere recursivement la memoire d'une commande
void liberer_command(Command *cmd)
{
    liberer_arguments(&cmd->args);
    if (cmd->cmds)
    {
        for (uint32_t i = 0; i < cmd->nbcmds; i++)
        {
            liberer_command(&cmd->cmds[i]);
        }
        free(cmd->cmds);
        cmd->cmds = NULL;
    }
    cmd->nbcmds = 0;
    cmd->type = 0;
}