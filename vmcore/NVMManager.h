#ifndef __NVM_MANAGER_H__
#define __NVM_MANAGER_H__

int NVM_Inti(uint32_t baseAddress, uint32_t nvmSize);
int NVM_Format(uint32_t baseAddress, uint32_t nvmSize);
uint32_t NVM_Alloc(uint32_t size);
void NVM_Free(uint32_t address);

#endif