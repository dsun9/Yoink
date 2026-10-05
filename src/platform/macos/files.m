#import "macos_internal.h"
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
static bool file_error(PPError *error,const char *action) {
    char message[256];snprintf(message,sizeof(message),"%s: %s",action,strerror(errno));
    return pp_mac_error(error,PP_ERROR_PLATFORM,message);
}
FILE *pp_platform_open_file(const char *path,const char *mode,PPError *error) {
    if(!path||!mode||(strcmp(mode,"rb")&&strcmp(mode,"wb"))) {errno=EINVAL;file_error(error,"Invalid file path or mode");return NULL;}
    FILE *file=fopen(path,mode);if(!file)file_error(error,"Cannot open file");return file;
}
FILE *pp_platform_temporary_file(const char *target,char **out,PPError *error) {
    if(out)*out=NULL;
    if(!target||!*target||!out){errno=EINVAL;file_error(error,"Missing temporary file output");return NULL;}
    size_t length=strlen(target);
    if(length>SIZE_MAX-24){errno=ENAMETOOLONG;file_error(error,"Temporary path too long");return NULL;}
    char *path=malloc(length+24);
    if(!path){errno=ENOMEM;file_error(error,"Cannot allocate temporary path");return NULL;}
    snprintf(path,length+24,"%s.pointy-XXXXXX",target);
    int fd=mkstemp(path);
    if(fd<0){free(path);file_error(error,"Cannot create same-directory temporary file");return NULL;}
    FILE *file=fdopen(fd,"w+b");
    if(!file){close(fd);unlink(path);free(path);file_error(error,"Cannot open temporary stream");return NULL;}
    *out=path;return file;
}
bool pp_platform_file_seek(FILE *file,uint64_t offset) {return file&&offset<=INT64_MAX&&!fseeko(file,(off_t)offset,SEEK_SET);}
bool pp_platform_file_size(FILE *file,uint64_t *size) {
    if(!file||!size||fseeko(file,0,SEEK_END))return false;
    off_t length=ftello(file);if(length<0)return false;*size=(uint64_t)length;return pp_platform_file_seek(file,0);
}
bool pp_platform_flush_file(FILE *file,PPError *error) {
    if(!file){errno=EINVAL;return file_error(error,"Missing file");}
    return (!fflush(file)&&!fcntl(fileno(file),F_FULLFSYNC))||file_error(error,"Cannot flush project to stable storage");
}
bool pp_platform_replace_file(const char *temporary,const char *target,PPError *error) {
    if(!temporary||!target){errno=EINVAL;return file_error(error,"Missing replacement path");}
    /* Same-directory rename is atomic. Report failure only before the rename
     * commits; a subsequent directory flush cannot roll that commit back. */
    return !rename(temporary,target)||file_error(error,"Cannot atomically replace project file");
}
bool pp_platform_remove_file(const char *path,PPError *error) {
    if(!path){errno=EINVAL;return file_error(error,"Missing temporary path");}
    return !unlink(path)||file_error(error,"Cannot remove temporary file");
}
