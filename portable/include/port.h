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
int FLA_WriteData(uint32_t address, uint8_t*data, uint32_t length);
uint8_t FLA_read_u8(uint32_t address);
uint32_t FLA_read_u32(uint32_t address);
void FLA_write_u8(uint32_t address, uint8_t data);
void FLA_write_u32(uint32_t address, uint32_t data);


#endif