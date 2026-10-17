/* HOST-ONLY minimal PCM WAV loader for full-engine integration.
 * This is a test double: not a replacement for real SDL_LoadWAV_RW. */
#include <SDL/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static uint32_t le32(const unsigned char *x){return (uint32_t)x[0]|((uint32_t)x[1]<<8)|((uint32_t)x[2]<<16)|((uint32_t)x[3]<<24);}
static uint16_t le16(const unsigned char *x){return (uint16_t)(x[0]|(x[1]<<8));}
SDL_AudioSpec *SDL_LoadWAV(const char *path,SDL_AudioSpec *spec,Uint8 **data,Uint32 *bytes)
{
    FILE *f=fopen(path,"rb");if(!f) return NULL;
    unsigned char head[12];
    if(fread(head,1,12,f)!=12||memcmp(head,"RIFF",4)||memcmp(head+8,"WAVE",4)){fclose(f);return NULL;}
    int format_seen=0;
    for(int i=0;i<64;i++){
      unsigned char ch[8];if(fread(ch,1,8,f)!=8)break;
      uint32_t size=le32(ch+4);
      if(!memcmp(ch,"fmt ",4)&&size>=16 && size<=256){
        unsigned char fmt[256];if(fread(fmt,1,size,f)!=size)break;
        if(le16(fmt)!=1 || le16(fmt+14)!=16){fclose(f);return NULL;}
        spec->channels=le16(fmt+2);spec->freq=(int)le32(fmt+4);spec->format=AUDIO_S16SYS;
        format_seen=1;
      }
      else if(!memcmp(ch,"data",4)&&format_seen&&size>0&&size<(32u*1024*1024)){
        *data=malloc(size);if(!*data){fclose(f);return NULL;}
        if(fread(*data,1,size,f)!=size){free(*data);*data=NULL;break;}
        *bytes=size;fclose(f);
        printf("HOST_SIM: PASS real game #bgm WAV asset loaded: %s (%u bytes)\n",path,size);
        return spec;
      }
      else if(fseek(f,(long)size,SEEK_CUR)!=0)break;
      if(size&1)fseek(f,1,SEEK_CUR);
    }
    fclose(f);return NULL;
}
void SDL_FreeWAV(Uint8 *data){free(data);}
