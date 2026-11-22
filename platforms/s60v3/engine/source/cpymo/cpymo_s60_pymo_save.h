#ifndef CPYMO_S60_PYMO_SAVE_H
#define CPYMO_S60_PYMO_SAVE_H
#ifdef S60V3

#include "cpymo_engine.h"

enum { CPYMO_S60_PYMO_SAVE_COUNT = 101 };
size_t cpymo_s60_pymo_save_list(const cpymo_engine *e,
    unsigned char slots[CPYMO_S60_PYMO_SAVE_COUNT]);
error_t cpymo_s60_pymo_save_import(cpymo_engine *e, unsigned int source_slot,
    unsigned int dest_slot);

#endif
#endif
