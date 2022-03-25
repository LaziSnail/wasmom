#ifndef __PORT_H__
#define __PORT_H__

#include "types.h"
/*此文件定义移植曾需要实现的函数
*/

//关于内存管理

void *acalloc(size_t nmemb, size_t size, char *name) ;
void *arecalloc(void *ptr, size_t old_nmemb, size_t nmemb, size_t size, char *name) ;
void afree(void * mem);


//关于Flash管理
int FLA_readData(uint8_t*dst, uint32_t address, uint32_t length);
int FLA_WriteData(uint32_t address, uint8_t*data, uint32_t length);
uint8_t FLA_read_u8(uint32_t address);
uint32_t FLA_read_u32(uint32_t address);
void FLA_write_u8(uint32_t address, uint8_t data);
void FLA_write_u32(uint32_t address, uint32_t data);
void FLA_fill_u8(uint32_t address, uint8_t value, uint32_t length);

//用于调试的接口
//void DEBUG_PRINTF(char* fmt, ...);
#define DEBUG_PRINTF        printf
#endif