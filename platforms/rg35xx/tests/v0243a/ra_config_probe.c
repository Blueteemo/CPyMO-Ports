/* Uses the unmodified RA v1.14.0 config parser. No audio device/core is loaded.
 * Only file/path I/O adapters live in ra_posix_io.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <file/config_file.h>
int main(int argc,char **argv) {
    if (argc<2) return 2;
    config_file_t *conf=config_file_new_from_path_to_string(argv[1]);
    if (!conf) return 3;
    for (int i=2;i<argc;i++) if(!config_append_file(conf,argv[i])) return 4;
    float db=999;bool save=true,overrides=true;
    config_get_float(conf,"audio_volume",&db);
    config_get_bool(conf,"config_save_on_exit",&save);
    config_get_bool(conf,"auto_overrides_enable",&overrides);
    printf("{\"audio_volume\":%.6f,\"config_save_on_exit\":%s,\"auto_overrides_enable\":%s}",db,save?"true":"false",overrides?"true":"false");
    config_file_free(conf);
    return 0;
}
