#include "types.h"
#include "port.h"


/*
______________________________________________________
|Type|Size|                  |Type|Size|......
------------------------------------------------------
|Block0                             |Block1

Type        --  1 Byte:00 means free, 01 means used, other reserved.
Size        --  3 Bytes.
*/
#define BTYPE_FREE                  0
#define BTYPE_USED                  1

#define BLOCK_TYPE_OFFSET           0
#define BLOCK_SIZE_OFFSET           1
#define BLOCK_HEADER_SIZE           4

static uint32_t nvm_base;
static uint32_t nvm_limit;

int NVM_Inti(uint32_t baseAddress, uint32_t nvmSize){
    nvm_base = baseAddress;
    nvm_limit = baseAddress + nvmSize;
    DEBUG_PRINTF("Head of flash:%02x %02x %02x %02x",FLA_read_u8(baseAddress),FLA_read_u8(baseAddress+1),FLA_read_u8(baseAddress+2),FLA_read_u8(baseAddress+3));
    return 0;
}

int NVM_Format(uint32_t baseAddress, uint32_t nvmSize){
    nvmSize = nvmSize&0x00FFFFFF;
    FLA_write_u32(baseAddress, nvmSize-BLOCK_HEADER_SIZE);
    return 0;
}

uint32_t NVM_Alloc(uint32_t size){
    uint32_t tmpaddr = nvm_base;
    uint8_t blocktype;
    uint32_t blocksize;

    if (size == 0){
        printf("NVM_Alloc:Alloc for 0 size.\r\n");
    	return 0;
    }
    while(tmpaddr < nvm_limit){
        //DEBUG_PRINTF("NVM_Alloc:Check Block at address:%x\r\n",tmpaddr);
        blocksize = FLA_read_u32(tmpaddr);
        blocktype = (blocksize&0xFF000000)>>24;
        blocksize = blocksize&0x00FFFFFF;
        //DEBUG_PRINTF("NVM_Alloc:Block size is %x Block type is %x\r\n",blocksize,blocktype);
        if (blocktype == BTYPE_FREE){
            if (blocksize >= size + BLOCK_HEADER_SIZE){
                FLA_write_u32(tmpaddr, blocksize - size - BLOCK_HEADER_SIZE);
                tmpaddr = tmpaddr + blocksize - size ;//- BLOCK_HEADER_SIZE;
                FLA_write_u32(tmpaddr, (size&0x00FFFFFF)|0x01000000);
                return tmpaddr + BLOCK_HEADER_SIZE;
            }else if (blocksize == size){
                FLA_write_u32(tmpaddr, (size&0x00FFFFFF)|0x01000000);
                return tmpaddr + BLOCK_HEADER_SIZE;
            }
        }

        tmpaddr = tmpaddr + blocksize + BLOCK_HEADER_SIZE;
    }

    return 0;
}

void NVM_Free(uint32_t address){
    uint32_t tmpaddr = nvm_base;
    uint8_t blocktype;
    uint32_t blocksize;
    uint32_t lastfreeaddr = 0;
    uint32_t lastfreesize = 0;

    while(tmpaddr < nvm_limit){
        blocksize = FLA_read_u32(tmpaddr);
        blocktype = (blocksize&0xFF000000)>>24;
        blocksize = blocksize&0x00FFFFFF;

        if (blocktype == BTYPE_FREE){
            lastfreeaddr = tmpaddr;
            lastfreesize = blocksize;
        }else{
            if (tmpaddr + BLOCK_HEADER_SIZE == address){
                if (lastfreeaddr != 0){
                    lastfreesize += blocksize + BLOCK_HEADER_SIZE;
                    FLA_write_u32(lastfreeaddr, (lastfreesize&0x00FFFFFF));
                }else{
                    FLA_write_u32(tmpaddr, (blocksize&0x00FFFFFF));
                }
                return;
            }

            lastfreeaddr = 0;            

        }
        tmpaddr = tmpaddr + blocksize + BLOCK_HEADER_SIZE;
    }
}
