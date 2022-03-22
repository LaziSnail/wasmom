/*
使用一块RAM来模拟Flash
*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "types.h"
#define TOTAL_FLASH_SIZE        (1024*256)
uint8_t flash_buffer[TOTAL_FLASH_SIZE];

int FLA_WriteData(uint32_t address, uint8_t*data, uint32_t length){
    if (address + length <= TOTAL_FLASH_SIZE){
        memcpy(&flash_buffer[address], data, length);
        return length;
    }

    return 0;
}

int FLA_readData(uint8_t*dst, uint32_t address, uint32_t length){
    if (address + length < TOTAL_FLASH_SIZE){
        memcpy(dst, &flash_buffer[address], length);
        return length;
    }
    return 0;
}
uint8_t FLA_read_u8(uint32_t address){
    if (address  < TOTAL_FLASH_SIZE){
        return flash_buffer[address];
    }

    return 0xFF;
}
uint32_t FLA_read_u32(uint32_t address){
    uint32_t ret = 0xFFFFFFFF;
    if (address + 4 < TOTAL_FLASH_SIZE){
        //return *addr;
        //ret = flash_buffer[address]*0x01000000 + flash_buffer[address+1]*0x00010000 + flash_buffer[address+2]*0x00000100 + flash_buffer[address+3];
        ret = flash_buffer[address] + flash_buffer[address+1]*0x00000100 + flash_buffer[address+2]*0x00010000 + flash_buffer[address+3]*0x01000000;
    }
    return ret;
}
void FLA_write_u8(uint32_t address, uint8_t data){
    if (address  < TOTAL_FLASH_SIZE){
        flash_buffer[address] = data;
    }
    return;
}
void FLA_write_u32(uint32_t address, uint32_t data){
    if (address + 4 < TOTAL_FLASH_SIZE){
        /*
        flash_buffer[address] = (data>>24)&0x000000FF;
        flash_buffer[address+1] = (data>>16)&0x000000FF;
        flash_buffer[address+2] = (data>>8)&0x000000FF;
        flash_buffer[address+3] = data&0x000000FF;*/
        flash_buffer[address+3] = (data>>24)&0x000000FF;
        flash_buffer[address+2] = (data>>16)&0x000000FF;
        flash_buffer[address+1] = (data>>8)&0x000000FF;
        flash_buffer[address] = data&0x000000FF;
    }
    return;
}

void FLA_fill_u8(uint32_t address, uint8_t value, uint32_t length){
    uint32_t i;
    if (address + length < TOTAL_FLASH_SIZE){
        for (i=0;i<length;i++){
            flash_buffer[address + i] = value;
        }
    }
    return ;
}