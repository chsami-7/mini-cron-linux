#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <endian.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>

#include "command.h"

// Définitions des types de commandes
static const char *TYPES_COMMANDES[] = {"SI", "SQ", "PL", "IF"};
static const int NB_TYPES = 4;

/** Convertit un type de commande en sa représentation string */
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

/** Lit une chaîne de caractères depuis un descripteur de fichier */
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

/** Lit les arguments depuis un fichier argv */
Arguments lire_arguments(const char *argv_file)
{
    Arguments args = {NULL, 0};

    int fd = open(argv_file, O_RDONLY);
    if (fd == -1)
    {
        return args;
    }

    uint32_t argc;
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

/** Lit une commande depuis un répertoire */
Command lire_command(const char *cmd_dir)
{
    Command cmd = {0};

    char type_path[512], argv_path[512];

    snprintf(type_path, sizeof(type_path), "%s/type", cmd_dir);
    snprintf(argv_path, sizeof(argv_path), "%s/argv", cmd_dir);

    int fd = open(type_path, O_RDONLY);
    if (fd == -1)
    {
        return cmd;
    }

    uint16_t type_raw;
    if (read(fd, &type_raw, 2) != 2)
    {
        close(fd);
        return cmd;
    }
    cmd.type = be16toh(type_raw);
    close(fd);

    const char *type_str = type_vers_string(cmd.type);

    if (strcmp(type_str, "SI") == 0)
    {
        cmd.args = lire_arguments(argv_path);
    }
    else
    {
        cmd.args.argc = 0;
        cmd.args.argv = NULL;
        DIR *dir = opendir(cmd_dir);
        if (dir)
        {
            struct dirent *entry;
            cmd.nbcmds = 0;

            while ((entry = readdir(dir)) != NULL)
            {
                if (isdigit(entry->d_name[0]))
                {
                    cmd.nbcmds++;
                }
            }
            closedir(dir);

            if (cmd.nbcmds > 0)
            {
                cmd.cmds = malloc(cmd.nbcmds * sizeof(Command));
                if (cmd.cmds)
                {
                    for (uint32_t i = 0; i < cmd.nbcmds; i++)
                    {
                        char subcmd_path[512];
                        snprintf(subcmd_path, sizeof(subcmd_path), "%s/%u", cmd_dir, i);
                        cmd.cmds[i] = lire_command(subcmd_path);
                    }
                }
            }
        }
    }

    return cmd;
}

/** Affiche une commande */
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

/** Libère la mémoire allouée pour les arguments */
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

/** Libère la mémoire allouée pour une commande */
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
