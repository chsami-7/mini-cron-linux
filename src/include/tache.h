#ifndef TACHE_H 
#define TACHE_H

#include "timing.h"
#include "command.h"

typedef struct
{
    int id;
    char path[2048];
    Timing timing;
    Command cmd;
    time_t derniere_execution;
} Tache;

int executer_tache(const char *task_dir, const Command *cmd);

void afficher_tache(uint64_t task_id, const Timing *timing, const Command *cmd);
#endif