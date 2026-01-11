#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <endian.h>
#include <string.h>
#include <time.h>

#include "timing.h"

// lit un fichier timing binaire et remplit la structure
Timing lire_timing(const char *chemin) {
    Timing t = {0, 0, 0};
    int fd = open(chemin, O_RDONLY);
    if (fd == -1)
    {
        perror("Erreur ouverture fichier");
        return t;
    }

    uint8_t buffer[13];
    // on lit exactement 13 octets : 8 pour minutes, 4 pour heures, 1 pour jours
    if (read(fd, buffer, 13) != 13)
    {
        perror("Erreur lecture fichier");
        close(fd);
        return t;
    }
    close(fd);

    // conversion big endian vers hote
    t.minutes = be64toh(*(uint64_t *)buffer);
    t.heures = be32toh(*(uint32_t *)(buffer + 8));
    t.jours = buffer[12];

    return t;
}

// fonction de debug pour afficher le timing de facon lisible
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

// verifie si la tache doit etre executee a l'heure actuelle
int doit_executer(const Timing *t, const struct tm *now)
{
    // verifie si le bit de la minute actuelle est actif
    if (!(t->minutes & (1ULL << now->tm_min)))
        return 0;
    // verifie si le bit de l'heure actuelle est actif
    if (!(t->heures & (1U << now->tm_hour)))
        return 0;
    // verifie si le bit du jour actuel est actif
    if (!(t->jours & (1U << now->tm_wday)))
        return 0;

    return 1;
}

// ecrit la structure timing dans un descripteur de fichier en big endian
int serialise_timing(int fd_reponse, const Timing *t) {
    uint64_t minutes = htobe64(t->minutes);
    uint32_t heures = htobe32(t->heures);
    uint8_t jours = t->jours;

    write(fd_reponse, &minutes, 8);
    write(fd_reponse, &heures, 4);
    write(fd_reponse, &jours, 1);

    return 0;
}

// lit la structure timing depuis un descripteur de fichier
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

// convertit une chaine type '1,3-5' en masque binaire
uint64_t parse_list_to_bitmap(const char *arg, int min_val, int max_val)
{
    uint64_t mask = 0;
    if (!arg) return 0;
    
    char *s = strdup(arg);
    if (!s) return 0;
    
    char *token = s;
    char *end_token;
    
    // decoupage par virgules
    while ((end_token = strchr(token, ',')) != NULL || *token != '\0') {
        if (end_token) *end_token = '\0';
        
        char *dash = strchr(token, '-');
        // gestion des intervalles comme 2-5
        if (dash) {
            *dash = '\0';
            int start = atoi(token);
            int end = atoi(dash + 1);
            if (start < min_val) start = min_val;
            if (end > max_val) end = max_val;
            
            for (int i = start; i <= end; i++) mask |= (1ULL << i);
        } else {
            // gestion des valeurs simples
            int val = atoi(token);
            if (val >= min_val && val <= max_val) mask |= (1ULL << val);
        }
        
        if (end_token) token = end_token + 1;
        else break;
    }
    
    free(s);
    return mask;
}