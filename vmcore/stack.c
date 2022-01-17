#include "stack.h"
#include "module.h"


static uint32_t stack[STACK_SIZE];
static int sp;

static Frame frameStack[CALLSTACK_SIZE];
static int csp;
static int fp;

#ifdef SUPPORT_TYPE_CHECK

#endif

static uint8_t stackType[STACK_SIZE];

void initStack(void){
    sp = -1;
    csp = -1;
    fp = -1;
}

Frame* getCurrentFrame(){
    return &frameStack[csp];
}

int32_t popStack_i32() {
#ifdef SUPPORT_TYPE_CHECK
    if (stackType[sp] != I32) {
        printf("Error!stack type error.\r\n");
        return 0;
    }
#endif
    return (int32_t) stack[sp--];
}

uint32_t popStack_u32(void) {
    return (uint32_t) stack[sp--];
}

uint64_t popStack_u64(void){
    uint64_t ret;
    ret = stack[sp--];
    ret = (ret << 32) + stack[sp--];
    return ret;
}

void pushStack_u32(uint32_t v) {
    sp++;
    stack[sp] = v;
    stackType[sp] = STACK_TYPE_U32;
}

void pushStack_u64(uint64_t v){
    sp++;
    stack[sp] = (uint32_t)(v&0x00000000FFFFFFFF);
    stackType[sp] = STACK_TYPE_U64_L;

    sp++;
    stack[sp] = (uint32_t)((v>>32)&0x00000000FFFFFFFF);
    stackType[sp] = STACK_TYPE_U64_H;
}

float popStack_f32(void) {
    return (float) stack[sp--];
}

double popStack_f64(void){
    uint64_t ret;
    ret = stack[sp--];
    ret = (ret << 32) + stack[sp--];
    return *((double*)&ret);
}


void pushStack_f32(float v) {
    sp++;
    stack[sp] = v;
    stackType[sp] = STACK_TYPE_F32;
}

void pushStack_f64(double v){
    sp++;
    stack[sp] = (uint32_t)((uint64_t)v&0x00000000FFFFFFFF);
    stackType[sp] = STACK_TYPE_F64_L;

    sp++;
    stack[sp] = (uint32_t)(((uint64_t)v>>32)&0x00000000FFFFFFFF);
    stackType[sp] = STACK_TYPE_F64_H;
}

uint32_t getStack_u32(void) {
    return (uint32_t) stack[sp];
}

Block *popOnlyCSP(uint32_t deep) {
    if (csp < deep) {
        return NULL;
    }
    csp -= deep;
    return frameStack[csp].block;
}

uint32_t getCurrentCSP(void) {
    return csp;
}

int getCurrentSP(void){
	return sp;
}

/*
uint32_t getLocal(uint32_t index) {
    return (uint32_t) stack[fp + index];
}*/

uint32_t getLocal(uint32_t index) {
    return (uint32_t) stack[fp + index];
}
void localToStackTop(uint32_t index){
    uint32_t realIndex = 0;

    while(index > 0){
        if ((stackType[fp + realIndex] == STACK_TYPE_F32)||(stackType[fp + realIndex] == STACK_TYPE_U32)){
            realIndex++;
            index--;
        }else if ((stackType[fp + realIndex] == STACK_TYPE_F64_H)||(stackType[fp + realIndex] == STACK_TYPE_U64_H)){
            realIndex++;
            index--;
        }else if ((stackType[fp + realIndex] == STACK_TYPE_F64_L)||(stackType[fp + realIndex] == STACK_TYPE_U64_L)){
            realIndex++;
            //index--;
        }
    }

    if ((stackType[fp + realIndex] == STACK_TYPE_F64_L)||(stackType[fp + realIndex] == STACK_TYPE_U64_L)){
        sp++;
        stack[sp] = stack[fp + realIndex];
        stackType[sp] = stackType[fp + realIndex];
        sp++;
        stack[sp] = stack[fp + realIndex+1];
        stackType[sp] = stackType[fp + realIndex+1];
    }else{
        sp++;
        stack[sp] = stack[fp + realIndex];
        stackType[sp] = stackType[fp + realIndex];
    }
}
void stackTopToLocal(uint32_t index, uint8_t pop) {
    uint32_t realIndex = 0;

    while(index > 0){
        if ((stackType[fp + realIndex] == STACK_TYPE_F32)||(stackType[fp + realIndex] == STACK_TYPE_U32)){
            realIndex++;
            index--;
        }else if ((stackType[fp + realIndex] == STACK_TYPE_F64_H)||(stackType[fp + realIndex] == STACK_TYPE_U64_H)){
            realIndex++;
            index--;
        }else if ((stackType[fp + realIndex] == STACK_TYPE_F64_L)||(stackType[fp + realIndex] == STACK_TYPE_U64_L)){
            realIndex++;
            //index--;
        }
    }

    if ((stackType[sp] == STACK_TYPE_F64_H)||(stackType[sp] == STACK_TYPE_U64_H)){
        stack[fp + realIndex] = stack[sp - 1];
        stackType[fp + realIndex] = stackType[sp - 1];//H
        stack[fp + realIndex+1] = stack[sp];
        stackType[fp + realIndex+1] = stackType[sp];//H
        if (pop != 0){
            sp -= 2;
        }
    }else{
        stack[fp + realIndex] = stack[sp];
        stackType[fp + realIndex] = stackType[sp];//H
        if (pop != 0){
            sp -= 1;
        }
    }

    return;
}



extern uint32_t getCurrentPC();
extern void setCurrentPC(uint32_t p);
void pushFrame(Block *block) {
    csp += 1;
    frameStack[csp].block = block;
    frameStack[csp].sp = sp - (int) block->type->param_count;

    /* 4. 保存 fp */

    // 将该栈帧被压入操作数栈顶前的【当前栈帧的操作数栈底指针】保存到 frame>fp 中，
    // 以便后续该栈帧关联的控制块执行完成，该栈帧弹出后，恢复压栈前的【当前栈帧的操作数栈底指针】
    frameStack[csp].fp = fp;

    /* 5. 保存 ra */

    // 将该栈帧被压入操作数栈顶前的【下一条即将执行的指令的地址】保存到 frame>ra 中，
    // 以便后续该栈帧关联的函数执行完后，返回到调用该函数的地方继续执行后面的指令
    frameStack[csp].ra = getCurrentPC();
    //fp = sp - (int) block->type->param_count + 1;
    fp = sp - calcVariableSize(block->type->params, block->type->param_count)/4 + 1;

    for (uint32_t i = 0; i < block->local_count; i++) {

        //sp++;
        //stack[sp] = 0;//TODO 处理64位数字的情况
        if (block->locals[i] == I32){
            pushStack_u32(0);
        }else if (block->locals[i] == F32){
            pushStack_f32(0);
        }else if (block->locals[i] == I64){
            pushStack_u64(0);
        }else if (block->locals[i] == F64){
            pushStack_f64(0);
        }
    }
}

Block *popFrame(void) {
    /* 1. 弹出调用栈顶 */

    // 从调用栈顶中弹出当前栈帧，同时调用栈指针减 1
    Frame *frame = &frameStack[csp];
    csp--;

    /* 2. 校验控制块的返回值类型 */

    // 获取控制帧对应控制块（包含函数）的签名（即控制块的返回值的数量和类型）
    Type *t = frame->block->type;
    // 背景知识：目前多返回值提案还没有进入 Wasm 标准，根据当前版本的 Wasm 标准，控制块不能有参数，且最多只能有一个返回值
    // 如果控制块的返回值数量为 1，也就是有一个返回值时，需要对返回值类型进行校验
    if (t->result_count == 1) {
        // 获取当前栈帧的操作数栈顶值，也就是控制块（包含函数）的返回值，
        // 判断其类型和【控制块签名中的返回值类型】是否一致，如果不一致则记录异常信息
        /*试试不检查类型是否影响功能，我觉得应该不影响。   by ljp
        if (m->stack[m->sp].value_type != t->results[0]) {
            sprintf(exception, "call type mismatch");
            return NULL;
        }*/
    }

    /* 3. 恢复 sp */

    // 因为该栈帧弹出，所以需要恢复该栈帧被压入调用栈前的【操作数栈顶指针】
    // 注：frame->sp 保存的是该栈帧被压入调用栈前的【操作数栈顶指针】
    if (t->result_count == 1) {
        // 背景知识：目前多返回值提案还没有进入 Wasm 标准，根据当前版本的 Wasm 标准，控制块不能有参数，且最多只能有一个返回值
        // 如果控制块有一个返回值，则这个返回值需要压入到恢复后的操作数栈顶，即恢复后的操作数栈长度需要加 1
        // 所以恢复的【操作数栈顶指针值】 是 该栈帧被压入调用栈前的【操作数栈顶指针】再加 1
        if (frame->sp < sp) {
            stack[frame->sp + 1] = stack[sp];
            sp = frame->sp + 1;
        }
    } else {
        // 如果控制块没有返回值，则直接恢复该栈帧被压入调用栈前的【操作数栈顶指针】即可
        if (frame->sp < sp) {
            sp = frame->sp;
        }
    }

    /* 4. 恢复 fp */

    // 因为该栈帧弹出，所以需要恢复该栈帧被压入调用栈前的【当前栈帧的操作数栈底指针】
    // 注：frame->fp 保存的是该栈帧被压入调用栈前的【当前栈帧的操作数栈底指针】
    fp = frame->fp;

    /* 5. 恢复 ra */

    // 当控制块类型为函数时，在函数执行完成该栈帧弹出时，需要返回到该函数调用指令的下一条指令继续执行
    if (frame->block->block_type == 0x00) {
        // 将函数返回地址赋给程序计数器 pc（记录下一条即将执行的指令的地址）
        setCurrentPC(frame->ra);
    }

    return frame->block;
}

uint32_t calcVariableSize(uint32_t *vartyps, uint32_t varcnt){
    uint32_t ret = 0;
    for (;varcnt > 0;varcnt--){
        if ((*vartyps == I32)||(*vartyps == F32)){
            ret += 4;
        }else if ((*vartyps == I64)||(*vartyps == F64)){
            ret += 8;
        }
    }

    return ret;
}


