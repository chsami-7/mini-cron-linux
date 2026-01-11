#ifndef IO_UTILS_H
#define IO_UTILS_H

#include <stddef.h>   
#include <stdint.h>   

// ecrit tout le buffer dans le fichier meme si le systeme coupe l'ecriture
void write_full(int fd, const void *buf, size_t count);

// lit exactement le nombre d'octets demande en gerant les interruptions
int  read_full(int fd, void *buf, size_t count);

// envoie un message d'erreur standardise au client
void send_error(int fd, uint16_t code);

#endif