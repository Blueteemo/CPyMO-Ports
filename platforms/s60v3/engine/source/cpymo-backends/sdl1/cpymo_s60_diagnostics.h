#ifndef CPYMO_S60_DIAGNOSTICS_H
#define CPYMO_S60_DIAGNOSTICS_H

#ifdef S60V3
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

int cpymo_s60_diagnostics_enabled(void);

#ifdef __cplusplus
}
#endif

#define CPYMO_S60_TRACE(...) do { \
    if (cpymo_s60_diagnostics_enabled()) printf(__VA_ARGS__); \
} while (0)
#endif

#endif
