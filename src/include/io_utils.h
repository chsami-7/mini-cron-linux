#ifndef IO_UTILS_H
#define IO_UTILS_H

#include <stddef.h>   // size_t
#include <stdint.h>   // uint16_t

void write_full(int fd, const void *buf, size_t count);
int  read_full(int fd, void *buf, size_t count);

void send_error(int fd, uint16_t code);

#endif

