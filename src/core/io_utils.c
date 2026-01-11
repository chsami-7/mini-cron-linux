#include "io_utils.h"
#include <unistd.h>
#include <endian.h>

// s'assure que tout le buffer est bien ecrit dans le fichier
void write_full(int fd, const void *buf, size_t count)
{
    size_t total = 0;
    const char *ptr = buf;

    // boucle pour gerer les ecritures partielles
    while (total < count) {
        ssize_t n = write(fd, ptr + total, count - total);
        if (n <= 0) return;
        total += n;
    }
}

// s'assure de lire exactement le nombre d'octets demande
int read_full(int fd, void *buf, size_t count)
{
    size_t total = 0;
    char *ptr = buf;

    // boucle tant qu'on a pas tout lu
    while (total < count) {
        ssize_t n = read(fd, ptr + total, count - total);
        if (n <= 0) return -1;
        total += n;
    }
    return 0;
}

// envoie un code d'erreur au client
void send_error(int fd, uint16_t code)
{
    uint16_t ans = htobe16(0x4552); // 'ER'
    uint16_t err = htobe16(code);
    write_full(fd, &ans, 2);
    write_full(fd, &err, 2);
}