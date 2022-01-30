#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include "interpreter.h"
#include "module.h"
#include "stack.h"
#include "NVMManager.h"
#include "port.h"
#include "types.h"

extern uint8_t flash_buffer[];
int main(int argc, char* argv[]){
    struct Module *m;
    FILE *fp;
    int fsize;
    int i;
    int res;
    uint8_t * buffer;
    uint32_t tmp32;

    fp = fopen(argv[1], "r");
    if (!fp) {
        printf("Open file %s failed.\r\n",argv[1]);
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    buffer = malloc(fsize);
    fread(buffer,1,fsize,fp);
    
    printf("Load %d bytes from file:",fsize);
    for (i=0;i < fsize;i++){
        printf("%x",buffer[i]);
    }
    printf("\r\n");

    NVM_Format(0, 1024*256);
    NVM_Inti(0, 1024*256);
    m = load_module(buffer,1);
    tmp32 = NVM_Alloc(sizeof(Module));
    printf("Alloc flash at address %x\r\n",tmp32);
	FLA_WriteData(tmp32, m, sizeof(Module));
	afree(m);
	m = (Module *)tmp32;
    
    printf("Load module at flash:%p\r\n",m);

    initStack();
	Block *func = get_export(m, "add");
	//pushStackU32(m, 20);
	//pushStackU32(m, 24);
	pushStack_u32(20);
	pushStack_u32(24);
	res = invoke(m, func->fidx);
	if (res) {
		if (getCurrentSP() >= 0) {
			printf("func return:%d\n", popStack_u32());
			// 刷新标准输出缓冲区，把输出缓冲区里的东西打印到标准输出设备上，已实现及时获取执行结果
		}
	} else {
		//ERROR("Exception: %s\n", exception)
		printf("Erro!\r\n");
	}
    free(buffer);
    return 0;
}