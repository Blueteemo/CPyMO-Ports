/* Test-only stdio adapter. Configuration parsing/precedence and path handling
 * remain the unmodified RA 1.14.0 sources; no parser logic is duplicated here. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <file/file_path.h>
#include <streams/file_stream.h>
bool path_is_valid(const char *path) {struct stat st;return path&&stat(path,&st)==0;}
RFILE *filestream_open(const char *path,unsigned mode,unsigned hints) {
    (void)mode;(void)hints;return (RFILE*)fopen(path,"rb");
}
int filestream_eof(RFILE *f){return feof((FILE*)f);}
int filestream_close(RFILE *f){return fclose((FILE*)f);}
char *filestream_getline(RFILE *f){char *s=NULL;size_t n=0;if(getline(&s,&n,(FILE*)f)<0){free(s);return NULL;}return s;}
int64_t filestream_read_file(const char *path,void **buf,int64_t *len){
    FILE *f=fopen(path,"rb");if(!f)return 0;
    if(fseek(f,0,SEEK_END)){fclose(f);return 0;}long n=ftell(f);rewind(f);
    if(n<0){fclose(f);return 0;}char *s=malloc((size_t)n+1);if(!s){fclose(f);return 0;}
    if(fread(s,1,(size_t)n,f)!=(size_t)n){free(s);fclose(f);return 0;}s[n]=0;fclose(f);*buf=s;*len=n;return 1;
}
