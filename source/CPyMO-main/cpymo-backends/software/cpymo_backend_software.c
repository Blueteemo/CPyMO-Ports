#include "../../cpymo/cpymo_prelude.h"
#include "cpymo_backend_software.h"

cpymo_backend_software_context 
    *cpymo_backend_software_cur_context = NULL;

/* (float)k / 255.0f for all k, built without libm/startup dependencies. */
float cpymo_backend_software_u8f[256];

static bool cpymo_backend_software_u8f_ready = false;

void cpymo_backend_software_set_context(
    cpymo_backend_software_context *context)
{
    /* Fill lazily but before any pixel work: set_context() is called from
     * retro_load_game() before the first frame, and the singleton table is valid
     * for   the process lifetime.  Doing it here (rather than in a constructor
     * attribute) keeps the table correct for hosts that dlopen/dlclose repeatedly. */
    if (!cpymo_backend_software_u8f_ready) {
        for (int k = 0; k < 256; ++k)
            cpymo_backend_software_u8f[k] = (float)k / 255.0f;
        cpymo_backend_software_u8f_ready = true;
    }

    cpymo_backend_software_cur_context = context;
}
