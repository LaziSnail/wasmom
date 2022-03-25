#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include "interpreter.h"
#include "module.h"
#include "stack.h"
#include "NVMManager.h"
#include "port.h"
#include "types.h"
#include <stdarg.h>

/*
void DEBUG_PRINTF(char* fmt, ...){
    va_list args;
    va_start(args, fmt);
    printf(fmt, ...);
    va_end(args);
    return;
}*/


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
    
    

    NVM_Format(0, 1024*256);
    NVM_Inti(0, 1024*256);
    tmp32 = NVM_Alloc(fsize);
    DEBUG_PRINTF("Alloc flash at address %x\r\n",tmp32);
    DEBUG_PRINTF("address + len = %x\r\n",tmp32+fsize);
    if (FLA_WriteData(tmp32, buffer, fsize) == 0){
        DEBUG_PRINTF("FLA_WriteData failed!\r\n");
    }

    DEBUG_PRINTF("Load %d bytes from Flash:",fsize);
    for (i=0;i < fsize;i++){
        DEBUG_PRINTF("%02x",FLA_read_u8(tmp32 + i));
    }
    DEBUG_PRINTF("\r\n");

    m = load_module(tmp32,fsize);
    //tmp32 = NVM_Alloc(sizeof(Module));
	//FLA_WriteData(tmp32, m, sizeof(Module));
    DEBUG_PRINTF("Load module at flash:%p\r\n",m);

    initStack();
	uint32_t tmpFunc = get_export(m, "add");
	//pushStackU32(m, 20);
	//pushStackU32(m, 24);
	pushStack_u32(21);
	pushStack_u32(24);
	res = invoke(m, FLA_read_u32(tmpFunc + BLOCK_STRUCT_FIDX_OFFSET));
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