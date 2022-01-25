#include "port.h"
#include "types.h"

void afree(void * mem){
    vPortFree(mem);
    return;
}
// 申请内存
void *acalloc(size_t nmemb, size_t size, char *name) {
    void *res ;
    if (size*nmemb == 0){
    	return NULL;
    }
    res = pvPortMalloc(size*nmemb);//calloc(nmemb, size);

    if (res == NULL) {
    	//printf("!!Heap has only %d.\r\n",xPortGetFreeHeapSize());
        FATAL("Could not allocate %lu bytes for %s", nmemb * size, name);
    }
    memset(res, 0, size*nmemb);
    return res;
}

// 在原有内存基础上重新申请内存
void *arecalloc(void *ptr, size_t old_nmemb, size_t nmemb, size_t size, char *name) {
	void *res = pvPortMalloc(nmemb*size);

	if (res == NULL){
		//printf("!!Heap has only %d.\r\n",xPortGetFreeHeapSize());
		FATAL("Could not allocate %lu bytes for %s", nmemb * size, name);
	}
	memset(res, 0, nmemb*size);
 	memcpy(res, ptr, old_nmemb*size);

 	vPortFree(ptr);
	return res;
}