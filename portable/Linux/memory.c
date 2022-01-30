#include <stdlib.h>
#include <stdio.h>
#include "types.h"


void *acalloc(size_t nmemb, size_t size, char *name) {
    return calloc(nmemb, size);
}
void *arecalloc(void *ptr, size_t old_nmemb, size_t nmemb, size_t size, char *name) {
    return realloc(ptr, nmemb*size);
}

void afree(void * mem){
    free(mem);
    return;
}
