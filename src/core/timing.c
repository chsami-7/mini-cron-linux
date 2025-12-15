#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <endian.h>
#include <string.h>
#include <time.h>

#include "timing.h"


/*
 *  - Lit un fichier binaire de 13 octets représentant un timing
 *  - Le format du fichier est :
 *       [0..7]  = uint64 big-endian : masque des minutes (60 bits)
 *       [8..11] = uint32 big-endian : masque des heures  (24 bits)
 *       [12]    = uint8 : masque des jours (bits 0-6)
 *  Remplit une structure Timing avec ces valeurs décodées
 */
Timing lire_timing(const char *chemin) {
    Timing t = {0, 0, 0};
    int fd = open(chemin, O_RDONLY);
    if (fd == -1)
    {
        perror("Erreur ouverture fichier");
        return t;
    }

    uint8_t buffer[13];
    if (read(fd, buffer, 13) != 13)
    {
        perror("Erreur lecture fichier");
        close(fd);
        return t;
    }
    close(fd);

    t.minutes = be64toh(*(uint64_t *)buffer);
    t.heures = be32toh(*(uint32_t *)(buffer + 8));
    t.jours = buffer[12];

    return t;
}

void afficher_timing_generique(uint8_t jours, uint32_t heures, uint64_t minutes) {
    printf("=== DÉCODAGE GÉNÉRIQUE ===\n");

    printf("Jours activés: ");
    const char *noms_jours[] = {"Dim", "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam"};
    int nb_jours = 0;
    for (int i = 0; i < 7; i++)
    {
        if (jours & (1 << i))
        {
            if (nb_jours > 0)
                printf(", ");
            printf("%s", noms_jours[i]);
            nb_jours++;
        }
    }
    if (nb_jours == 0)
        printf("Aucun");
    printf("\n");

    printf("Heures activées: ");
    int nb_heures = 0;
    for (int i = 0; i < 24; i++)
    {
        if (heures & (1 << i))
        {
            if (nb_heures > 0)
                printf(", ");
            printf("%d", i);
            nb_heures++;
        }
    }
    if (nb_heures == 0)
        printf("Aucune");
    else if (nb_heures == 24)
        printf(" Toutes (0-23)");
    printf("\n");

    printf("Minutes activées: ");
    int nb_minutes = 0;
    for (int i = 0; i < 60; i++)
    {
        if (minutes & (1ULL << i))
        {
            nb_minutes++;
        }
    }

    if (nb_minutes == 0)
    {
        printf("Aucune");
    }
    else if (nb_minutes == 60)
    {
        printf(" Toutes (0-59)");
    }
    else
    {
        printf("%d minutes activées (", nb_minutes);
        int affiches = 0;
        for (int i = 0; i < 60 && affiches < 5; i++)
        {
            if (minutes & (1ULL << i))
            {
                if (affiches > 0)
                    printf(", ");
                printf("%d", i);
                affiches++;
            }
        }
        if (nb_minutes > 5)
        {
            printf(", ...");
        }
        printf(")");
    }
    printf("\n");
}

int doit_executer(const Timing *t, const struct tm *now)
{
    if (!(t->minutes & (1ULL << now->tm_min)))
        return 0;
    if (!(t->heures & (1U << now->tm_hour)))
        return 0;
    if (!(t->jours & (1U << now->tm_wday)))
        return 0;

    return 1;
}

int serialise_timing(int fd_reponse, const Timing *t) {
    uint64_t minutes = htobe64(t->minutes);
    uint32_t heures = htobe32(t->heures);
    uint8_t jours = t->jours;

    write(fd_reponse, &minutes, 8);
    write(fd_reponse, &heures, 4);
    write(fd_reponse, &jours, 1);

    return 0;
}

Timing deserialise_timing(int fd) {
    Timing t = {0, 0, 0};
    uint8_t buffer[13];
    if (read(fd, buffer, 13) != 13) {
        perror("Erreur lecture fichier");
        return t;
    }
    t.minutes = be64toh(*(uint64_t *)buffer);
    t.heures = be32toh(*(uint32_t *)(buffer + 8));
    t.jours = buffer[12];
    return t;
}
