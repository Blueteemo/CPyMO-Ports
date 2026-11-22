/* ARM/uClibc Libretro dlopen smoke probe for QEMU user mode (no SDL/display).
 * It only verifies ELF loader and required core symbols, not RG35XX GPU/input.
 */
#include <dlfcn.h>
#include <stdio.h>
int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s /absolute/path/cpymo_libretro.so\n", argv[0]);
        return 2;
    }
    void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        fprintf(stderr, "FAIL dlopen: %s\n", dlerror());
        return 3;
    }
    unsigned (*api_version)(void) = (unsigned (*)(void))dlsym(handle, "retro_api_version");
    if (!api_version) {
        fprintf(stderr, "FAIL dlsym: %s\n", dlerror());
        dlclose(handle);
        return 4;
    }
    unsigned version = api_version();
    printf("ARM_UCLIBC_DLOPEN_OK retro_api_version=%u\n", version);
    dlclose(handle);
    return version == 1 ? 0 : 5;
}
