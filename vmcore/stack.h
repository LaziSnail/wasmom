#ifndef __WASM_STACK_H__
#define __WASM_STACK_H__

#include <stdint.h>
#include <stdlib.h>
#include "module.h"

//#define SUPPORT_TYPE_CHECK

#define STACK_SIZE 128   // 操作数栈的容量 65536，即 64 * 1024，也就是 64KB
#define CALLSTACK_SIZE 64// 调用栈的容量 4096，即 4 * 1024，也就是 4KB

#define STACK_TYPE_U32          0xCC
#define STACK_TYPE_F32          0xCC
#define STACK_TYPE_U64_H        0x55
#define STACK_TYPE_U64_L        0xAA
#define STACK_TYPE_F64_H        0x55
#define STACK_TYPE_F64_L        0xAA

int32_t popStack_i32();
uint32_t popStack_u32();
void pushStack_u32(uint32_t v);
uint32_t getStack_u32(void);
void pushFrame(uint32_t block);
uint32_t popFrame(void);
Block *popOnlyCSP(uint32_t deep);
uint32_t getCurrentCSP(void);
uint32_t getLocal(uint32_t index);
void setLocal(uint32_t index, uint32_t value);
float popStack_f32(void);
void pushStack_f32(float v);
void initStack(void);
int getCurrentSP(void);
Frame* getCurrentFrame();
void stackTopToLocal(uint32_t index, uint8_t pop);
void localToStackTop(uint32_t index);
uint32_t calcVariableSize(uint32_t vartyps_addr, uint32_t varcnt);
#endif
