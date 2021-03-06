#include "module.h"
#include "interpreter.h"
#include "opcode.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port.h"
#include "types.h"
#include "utils.h"
#include "NVMManager.h"

uint32_t MODULE_getFunction(uint32_t m , uint32_t fidx){
    uint32_t t32;
    t32 = FLA_read_u32(m + MODULE_STRUCT_FUNCTION_ADDRESS_OFFSET);
    return FLA_read_u32(t32 + fidx*sizeof(uint32_t));
}

uint32_t MODULE_getBlockByPOS(uint32_t m , uint32_t pos){
    uint32_t lookupTableBase;
    uint32_t tableCount;
    uint32_t tmpblock;
    uint32_t i;
    lookupTableBase = FLA_read_u32(m + MODULE_STRUCT_BLOCK_LOOKUP_ADDRESS_OFFSET);
    tableCount = FLA_read_u32(m + MODULE_STRUCT_BLOCK_LOOKUP_COUNT_OFFSET);
    for (i=0;i < tableCount;i++){
        tmpblock = lookupTableBase + i*LOOKUP_TABLE_ITEM_SIZE;
        if (FLA_read_u32(tmpblock + LOOKUP_TABLE_ITEM_POS) == pos){
            return FLA_read_u32(tmpblock + LOOKUP_TABLE_ITEM_BLOCK);
        }
    }
    return NULL;
}
// 在单条指令中，除了占一个字节的操作码之外，后面可能也会紧跟着立即数，如果有立即数，则直接跳过立即数
// 注：指令是否存在立即数，是由操作数的类型决定，这也是 Wasm 标准规范的内容之一
void skip_immediate(uint32_t bytes, uint32_t *pos) {
    // 读取操作码
    uint32_t opcode = FLA_read_u8(bytes + *pos);//bytes[*pos];
    uint32_t count;
    *pos = *pos + 1;
    // 根据操作码类型，判断其有占多少位的立即数（或者没有立即数），并直接跳过该立即数
    switch (opcode) {
        /*
         * 控制指令
         * */
        case Block_:
        case Loop:
        case If:
            // Block_/Loop/If 指令的立即数有两部分，第一部分表示控制块的返回值类型（占 1 个字节），
            // 第二部分为子表达式（Block_/Loop 有一个子表达式，If 有两个子表达式）
            // 注：子表达式无需跳过，因为 find_block 主要就是要从控制块的表达式（包括子表达式）收集控制块的相关信息
            read_LEB_unsigned(bytes, pos, 7);
            break;
        case Br:
        case BrIf:
            // 跳转指令的立即数表示跳转的目标标签索引（占 4 个字节）
            read_LEB_unsigned(bytes, pos, 32);
            break;
        case BrTable:
            // BrTable 指令的立即数是指定的 n+1 个跳转的目标标签索引（每个索引值占 4 个字节）
            // 其中前 n 个目标标签索引构成一个索引表，最后 1 个标签索引为默认索引
            // 最终跳转到哪一个目标标签索引，需要在运行期间才能决定
            count = read_LEB_unsigned(bytes, pos, 32);
            for (uint32_t i = 0; i < count; i++) {
                read_LEB_unsigned(bytes, pos, 32);
            }
            read_LEB_unsigned(bytes, pos, 32);
            break;
        case Call:
            // Call 指令的立即数表示被调用函数的索引（占 4 个字节）
            read_LEB_unsigned(bytes, pos, 32);
            break;
        case CallIndirect:
            // CallIndirect 指令有两个立即数，第一个立即数表示被调用函数的类型索引（占 4 个字节），
            // 第二个立即数为保留立即数（占 1 个比特位），暂无用途
            read_LEB_unsigned(bytes, pos, 32);
            read_LEB_unsigned(bytes, pos, 1);
            break;

        /*
         * 变量指令
         * */
        case LocalGet:
        case LocalSet:
        case LocalTee:
        case GlobalGet:
        case GlobalSet:
            // 变量指令的立即数用于表示全局/局部变量的索引（占 4 个字节）
            read_LEB_unsigned(bytes, pos, 32);
            break;

        /*
         * 内存指令
         * */
        case I32Load ... I64Store32:
            // 内存加载/存储指令有两个立即数，第一个立即数表示内存偏移量（占 4 个字节），
            // 第二个立即数表示对齐提示（占 4 个字节）
            read_LEB_unsigned(bytes, pos, 32);
            read_LEB_unsigned(bytes, pos, 32);
            break;
        case MemorySize:
        case MemoryGrow:
            // 内存大小/增加指令的立即数表示所操作的内存索引（占 1 个比特位）
            // 由于当前 Wasm 规范规定一个模块最多只能导入或定义一块内存，所以目前必须为 0
            read_LEB_unsigned(bytes, pos, 1);
            break;
        case I32Const:
            // I32Const 指令的立即数表示 32 有符号整数（占 4 个字节）
            read_LEB_unsigned(bytes, pos, 32);
        case I64Const:
            // F32Const 指令的立即数表示 64 有符号整数（占 8 个字节）
            read_LEB_unsigned(bytes, pos, 64);
            break;
        case F32Const:
            // F32Const 指令的立即数表示 32 位浮点数（占 4 个字节）
            // 注：LEB128 编码仅针对整数，而该指令的立即数为浮点数，并没有被编码，而是直接写入到 Wasm 二进制文件中的
            *pos += 4;
            break;
        case F64Const:
            // F64Const 指令的立即数表示 64 位浮点数（占 8 个字节）
            // 注：LEB128 编码仅针对整数，而该指令的立即数为浮点数，并没有被编码，而是直接写入到 Wasm 二进制文件中的
            *pos += 8;
            break;
        default:
            // 其他操作码没有立即数
            // 注：Wasm 指令大部分指令没有立即数
            break;
    }
}
void find_blocks(uint32_t m){
    uint32_t tmpFunction;
    uint32_t tmpBlock;
    uint32_t t32;
    uint32_t blockcount;
    uint8_t opcode = Unreachable;
    uint32_t mbytes;
    int top = -1;
    uint32_t blockstack[BLOCKSTACK_SIZE];

    mbytes = FLA_read_u32(m + MODULE_STRUCT_BYTE_ADDRESS_OFFSET);
    // 先循环一边，算出一共有多少个Block
    blockcount = 0;

    for (uint32_t f = FLA_read_u32(m + MODULE_STRUCT_IMPORT_FUNC_COUNT_OFFSET); f < FLA_read_u32(m + MODULE_STRUCT_FUNCTION_COUNT_OFFSET); f++) {
        t32 = FLA_read_u32(m + MODULE_STRUCT_FUNCTION_ADDRESS_OFFSET);
        tmpFunction = FLA_read_u32(t32 + f*sizeof(uint32_t));

        // 从该函数的字节码部分的【起始地址】开始收集 Block_/Loop/If 控制块的相关信息--遍历字节码中的每条指令
        uint32_t pos = FLA_read_u32(tmpFunction + BLOCK_STRUCT_START_ADDRESS_OFFSET);
        
        while (pos <= FLA_read_u32(tmpFunction + BLOCK_STRUCT_END_ADDRESS_OFFSET)) {
            opcode = FLA_read_u8(mbytes + pos);
            switch (opcode) {
                case Block_:
                case Loop:
                case If:
                    blockcount++;
                    break;
                default:
                    break;
            }
            skip_immediate(mbytes, &pos);
        }
    }
    //根据Block数量分配空间
    t32 = NVM_Alloc(LOOKUP_TABLE_ITEM_SIZE * blockcount);
    FLA_write_u32(m + MODULE_STRUCT_BLOCK_LOOKUP_ADDRESS_OFFSET, t32);
    FLA_write_u32(m + MODULE_STRUCT_BLOCK_LOOKUP_COUNT_OFFSET, blockcount);

    for (uint32_t f = FLA_read_u32(m + MODULE_STRUCT_IMPORT_FUNC_COUNT_OFFSET); f < FLA_read_u32(m + MODULE_STRUCT_FUNCTION_COUNT_OFFSET); f++) {
        t32 = FLA_read_u32(m + MODULE_STRUCT_FUNCTION_ADDRESS_OFFSET);
        tmpFunction = FLA_read_u32(t32 + f*sizeof(uint32_t));

        // 从该函数的字节码部分的【起始地址】开始收集 Block_/Loop/If 控制块的相关信息--遍历字节码中的每条指令
        uint32_t pos = FLA_read_u32(tmpFunction + BLOCK_STRUCT_START_ADDRESS_OFFSET);
        
        while (pos <= FLA_read_u32(tmpFunction + BLOCK_STRUCT_END_ADDRESS_OFFSET)) {
            opcode = FLA_read_u8(mbytes + pos);
            switch (opcode) {
                case Block_:
                case Loop:
                case If:
                    // 如果操作码为 Block_/Loop/If 之一，则声明一个 Block 结构体
                    tmpBlock = NVM_Alloc(BLOCK_STRUCT_SIZE);

                    // 设置控制块的块类型：Block_/Loop/If
                    //block->block_type = opcode;
                    FLA_write_u8(tmpBlock + BLOCK_STRUCT_BLOCK_TYPE_OFFSET, opcode);

                    // 由于 Block_/Loop/If 操作码的立即数用于表示该控制块的类型（占一个字节）
                    // 所以可以根据该立即数，来获取控制块的类型，即控制块的返回值的数量和类型

                    // get_block_type 根据表示该控制块的类型的值（占一个字节），返回控制块的签名，即控制块的返回值的数量和类型
                    // 0x7f 表示有一个 i32 类型返回值、0x7e 表示有一个 i64 类型返回值、0x7d 表示有一个 f32 类型返回值、0x7c 表示有一个 f64 类型返回值、0x40 表示没有返回值
                    // 注：目前多返回值提案还没有进入 Wasm 标准，根据当前版本的 Wasm 标准，控制块不能有参数，且最多只能有一个返回值
                    //block->type = get_block_type(m->bytes[pos + 1]);
                    t32 = get_block_type(FLA_read_u8(mbytes + pos + 1));
                    FLA_write_u32(tmpBlock + BLOCK_STRUCT_TYPE_ADDRESS_OFFSET, t32);


                    // 设置控制块的起始地址
                    //block->start_addr = pos;
                    FLA_write_u32(tmpBlock + BLOCK_STRUCT_START_ADDRESS_OFFSET, pos);

                    // 向控制块栈中添加该控制块对应结构体
                    blockstack[++top] = tmpBlock;
                    // 向 m->block_lookup 映射中添加该控制块对应结构体，其中 key 为对应操作码 Block_/Loop/If 的地址
                    //m->block_lookup[pos] = block;
                    t32 = FLA_read_u32(m + MODULE_STRUCT_BLOCK_LOOKUP_ADDRESS_OFFSET);
                    FLA_write_u32(t32 + top*LOOKUP_TABLE_ITEM_SIZE + LOOKUP_TABLE_ITEM_POS, pos);
                    FLA_write_u32(t32 + top*LOOKUP_TABLE_ITEM_SIZE + LOOKUP_TABLE_ITEM_BLOCK, tmpBlock);
                    break;
                case Else_:
                    // 如果当前控制块中存在操作码为 Else_ 的指令，则当前控制块的块类型必须为 If
                    //ASSERT(blockstack[top]->block_type == If, "Else not matched with if\n")
                    ASSERT(FLA_read_u8(blockstack[top] + BLOCK_STRUCT_BLOCK_TYPE_OFFSET) == If, "Else not matched with if\n")

                    // 将 Else_ 指令的下一条指令地址，设置为该控制块的 else_addr，即 else 分支对应的字节码的首地址，
                    // 便于后续虚拟机在执行指令时，根据条件跳转到 else 分支对应的字节码继续执行指令
                    //blockstack[top]->else_addr = pos + 1;
                    FLA_write_u32(blockstack[top] + BLOCK_STRUCT_ELSE_ADDRESS_OFFSET, pos+1);
                    break;
                case End_:
                    // 如果操作码 End_ 的地址就是函数的字节码部分的【结束地址】，说明该控制块为该函数的最后一个控制块，则直接退出
                    //if (pos == function->end_addr) {
                    if (pos == FLA_read_u32(tmpFunction + BLOCK_STRUCT_END_ADDRESS_OFFSET)) {
                        break;
                    }

                    // 如果执行了 End_ 指令，说明至少收集了一个控制块的相关信息，所以 top 不可能是初始值 -1，至少大于等于 0
                    ASSERT(top >= 0, "Blockstack underflow\n")

                    // 从控制块栈栈弹出该控制块
                    tmpBlock = blockstack[top--];

                    // 将操作码 End_ 的地址设置为控制块的结束地址
                    //block->end_addr = pos;
                    FLA_write_u32(tmpBlock + BLOCK_STRUCT_END_ADDRESS_OFFSET, pos);
                    // 设置控制块的跳转地址 br_addr
                    if (FLA_read_u8(tmpBlock + BLOCK_STRUCT_BLOCK_TYPE_OFFSET) == Loop) {
                        // 如果是 Loop 类型的控制块，需要循环执行，所以跳转地址就是该控制块开头指令（即 Loop 指令）的下一条指令地址
                        // 注：Loop 指令占用两个字节（1字节操作码 + 1字节操作数），所以需要加 2
                        //block->br_addr = block->start_addr + 2;
                        FLA_write_u32(tmpBlock + BLOCK_STRUCT_BR_ADDRESS_OFFSET, FLA_read_u32(tmpBlock + BLOCK_STRUCT_START_ADDRESS_OFFSET) + 2);
                    } else {
                        // 如果是非 Loop 类型的控制块，则跳转地址就是该控制块的结尾地址，也就是操作码 End_ 的地址
                        //block->br_addr = pos;
                        FLA_write_u32(tmpBlock + BLOCK_STRUCT_BR_ADDRESS_OFFSET,pos);
                    }
                    break;
                default:
                    break;
            }
            skip_immediate(mbytes, &pos);
        }
    }
}
// 收集所有本地模块定义的函数中 Block_/Loop/If 控制块的相关信息，例如起始地址、结束地址、跳转地址、控制块类型等，
// 便于后续虚拟机解释执行指令时可以借助这些信息
#if 0
void find_blocks_delete(uint32_t m) {
    Block *function;
    Block *block;
    // 声明用于在遍历过程中存储控制块 block 的相关信息的栈
    Block *blockstack[BLOCKSTACK_SIZE];
    int top = -1;
    uint8_t opcode = Unreachable;

    // 遍历 m->functions 中所有的本地模块定义的函数，从每个函数字节码部分中收集 Block_/Loop/If 控制块的相关信息
    // 注：跳过从外部模块导入的函数，原因是导入函数的执行只需要执行 func_ptr 指针所指向的真实函数即可，无需通过虚拟机执行指令的方式
    //for (uint32_t f = m->import_func_count; f < m->function_count; f++) {
    for (uint32_t f = FLA_read_u32(m + MODULE_STRUCT_IMPORT_FUNC_COUNT_OFFSET); f < FLA_read_u32(m + MODULE_STRUCT_FUNCTION_COUNT_OFFSET); f++) {

        // 获取单个函数对应的结构体
        function = &m->functions[f];

        // 从该函数的字节码部分的【起始地址】开始收集 Block_/Loop/If 控制块的相关信息--遍历字节码中的每条指令
        uint32_t pos = function->start_addr;
        // 直到该函数的字节码部分的【结束地址】结束
        while (pos <= function->end_addr) {
            // 每次 while 循环都会分析一条指令，而每条指令都是以占单个字节的操作码开始

            // 获取操作码，根据操作码类型执行不同逻辑
            //opcode = m->bytes[pos];
            opcode = FLA_read_u8(m->bytes + pos);
            switch (opcode) {
                case Block_:
                case Loop:
                case If:
                    // 如果操作码为 Block_/Loop/If 之一，则声明一个 Block 结构体
                    block = acalloc(1, sizeof(Block), "Block");

                    // 设置控制块的块类型：Block_/Loop/If
                    block->block_type = opcode;

                    // 由于 Block_/Loop/If 操作码的立即数用于表示该控制块的类型（占一个字节）
                    // 所以可以根据该立即数，来获取控制块的类型，即控制块的返回值的数量和类型

                    // get_block_type 根据表示该控制块的类型的值（占一个字节），返回控制块的签名，即控制块的返回值的数量和类型
                    // 0x7f 表示有一个 i32 类型返回值、0x7e 表示有一个 i64 类型返回值、0x7d 表示有一个 f32 类型返回值、0x7c 表示有一个 f64 类型返回值、0x40 表示没有返回值
                    // 注：目前多返回值提案还没有进入 Wasm 标准，根据当前版本的 Wasm 标准，控制块不能有参数，且最多只能有一个返回值
                    //block->type = get_block_type(m->bytes[pos + 1]);
                    block->type = get_block_type(FLA_read_u8(m->bytes + pos+1));
                    // 设置控制块的起始地址
                    block->start_addr = pos;

                    // 向控制块栈中添加该控制块对应结构体
                    blockstack[++top] = block;
                    // 向 m->block_lookup 映射中添加该控制块对应结构体，其中 key 为对应操作码 Block_/Loop/If 的地址
                    m->block_lookup[pos] = block;
                    break;
                case Else_:
                    // 如果当前控制块中存在操作码为 Else_ 的指令，则当前控制块的块类型必须为 If
                    ASSERT(blockstack[top]->block_type == If, "Else not matched with if\n")

                    // 将 Else_ 指令的下一条指令地址，设置为该控制块的 else_addr，即 else 分支对应的字节码的首地址，
                    // 便于后续虚拟机在执行指令时，根据条件跳转到 else 分支对应的字节码继续执行指令
                    blockstack[top]->else_addr = pos + 1;
                    break;
                case End_:
                    // 如果操作码 End_ 的地址就是函数的字节码部分的【结束地址】，说明该控制块为该函数的最后一个控制块，则直接退出
                    if (pos == function->end_addr) {
                        break;
                    }

                    // 如果执行了 End_ 指令，说明至少收集了一个控制块的相关信息，所以 top 不可能是初始值 -1，至少大于等于 0
                    ASSERT(top >= 0, "Blockstack underflow\n")

                    // 从控制块栈栈弹出该控制块
                    block = blockstack[top--];

                    // 将操作码 End_ 的地址设置为控制块的结束地址
                    block->end_addr = pos;
                    // 设置控制块的跳转地址 br_addr
                    if (block->block_type == Loop) {
                        // 如果是 Loop 类型的控制块，需要循环执行，所以跳转地址就是该控制块开头指令（即 Loop 指令）的下一条指令地址
                        // 注：Loop 指令占用两个字节（1字节操作码 + 1字节操作数），所以需要加 2
                        block->br_addr = block->start_addr + 2;
                    } else {
                        // 如果是非 Loop 类型的控制块，则跳转地址就是该控制块的结尾地址，也就是操作码 End_ 的地址
                        block->br_addr = pos;
                    }
                    break;
                default:
                    break;
            }
            // 在单条指令中，除了占一个字节的操作码之外，后面可能也会紧跟着立即数，如果有立即数，则直接跳过立即数去处理下一条指令的操作码
            // 注：指令是否存在立即数，是由操作数的类型决定，这也是 Wasm 标准规范的内容之一
            skip_immediate(m->bytes, &pos);
        }
        // 当执行完 End_ 分支后，top 应该重新回到 -1，否则就是没有执行 End_ 分支
        ASSERT(top == -1, "Function ended in middle of block\n")
        // 控制块应该以操作码 End_ 结束
        ASSERT(opcode == End_, "Function block did not end with 0xb\n")
    }
}
#endif
// 解析表段中的表 table_type（目前表段只会包含一张表）
// 表 table_type 编码如下：
// table_type: 0x70|limits
// limits: flags|min|(max)?
// 注：之所以要封装成独立函数，是因为在 load_module 函数中有两次调用：1.解析本地定义的表段；2. 解析从外部导入的表
void parse_table_type(uint32_t m, uint32_t *pos) {
    uint32_t mbytes;
    uint32_t table; 
    // 由于表段中只会有一张表，所以无需遍历

    mbytes = FLA_read_u32(m + MODULE_STRUCT_BYTE_ADDRESS_OFFSET);
    table = FLA_read_u32(m + MODULE_STRUCT_TABLE_ADDRESS_OFFSET);
    // 表中的元素必需为函数引用，所以编码必需为 0x70
    //m->table.elem_type = read_LEB_unsigned(m->bytes, pos, 7);
    FLA_write_u8(table + TABLE_STRUCT_ELEM_TYPE_OFFSET, (uint8_t) read_LEB_unsigned(mbytes, pos, 7));
    //ASSERT(m->table.elem_type == ANYFUNC, "Table elem_type 0x%x unsupported\n", m->table.elem_type)

    // flags 为标记位，如果为 0 表示只需指定表中元素数量下限；为 1 表示既要指定表中元素数量的上限，又指定表中元素数量的下限
    uint32_t flags = read_LEB_unsigned(mbytes, pos, 32);
    // 先读取表中元素数量下限，同时设置为该表的当前元素数量
    uint32_t tsize = read_LEB_unsigned(mbytes, pos, 32);
    //m->table.min_size = tsize;
    //m->table.cur_size = tsize;
    FLA_write_u32(table + TABLE_STRUCT_MIN_SIZE_OFFSET, tsize);
    FLA_write_u32(table + TABLE_STRUCT_CUR_SIZE_OFFSET, tsize);
    // flags 为 1 表示既要指定表中元素数量的上限，又指定表中元素数量的下限
    if (flags & 0x1) {
        // 读取表中元素数量的上限
        tsize = read_LEB_unsigned(mbytes, pos, 32);
        // 表的元素数量最大上限为 64K，如果读取的表的元素数量上限值超过 64K，则默认设置 64K，否则设置为读取的值即可
        //m->table.max_size = (uint32_t) fmin(0x10000, tsize);
        FLA_write_u32(table + TABLE_STRUCT_MAX_SIZE_OFFSET, (uint32_t) fmin(0x10000, tsize));
    } else {
        // flags 为 0，表示没有特别指定表的元素数量上限，所以设置为默认的 64K 即可
        //m->table.max_size = 0x10000;
        FLA_write_u32(table + TABLE_STRUCT_MAX_SIZE_OFFSET, 0x10000);
    }
}

// 解析内存段中的内存 mem_type（目前内存段只会包含一块内存）
// 内存 mem_type 编码如下：
// mem_type: limits
// limits: flags|min|(max)?
// 注：之所以要封装成独立函数，是因为在 load_module 函数中有两次调用：1.解析本地定义的内存段；2. 解析从外部导入的内存
void parse_memory_type(uint32_t m, uint32_t *pos) {
    uint32_t mbytes;
    // 由于内存段中只会有一块内存，所以无需遍历
    mbytes = FLA_read_u32(m + MODULE_STRUCT_BYTE_ADDRESS_OFFSET);
    // flags 为标记位，如果为 0 表示只指定内存大小的下限；为 1 表示既指定内存大小的上限，又指定内存大小的下限
    uint32_t flags = read_LEB_unsigned(mbytes, pos, 32);
    // 先读取内存大小的下限，并设置为该内存的初始大小
    uint32_t pages = read_LEB_unsigned(mbytes, pos, 32);
    //m->memory.min_size = pages;
    //m->memory.cur_size = pages;

    // flags 为 1 表示既指定内存大小上限，又指定内存大小下限
    if (flags & 0x1) {
        // 读取内存大小上限
        pages = read_LEB_unsigned(mbytes, pos, 32);
        // 内存大小最大上限为 2GB，如果读取的内存大小上限值超过 2GB，则默认设置 2GB，否则设置为读取的值即可
        //m->memory.max_size = (uint32_t) fmin(0x8000, pages);
    } else {
        // flags 为 0，表示没有特别指定内存大小上限，所以设置为默认的 2GB 即可
        //m->memory.max_size = 0x8000;
    }
}

// 解析 Wasm 二进制文件内容，将其转化成内存格式 Module，以便后续虚拟机基于此执行对应指令
uint32_t load_module(uint32_t bytes, const uint32_t byte_count) {
    // 用于标记解析 Wasm 二进制文件第 pos 个字节
    uint32_t pos = 0;
    uint32_t tmp32;
    uint64_t tmp64;
    uint32_t start_function;
    uint32_t local_global_count = 0;
    uint64_t* local_globals;

    // 声明内存格式对应的结构体 m
    uint32_t m;

    // 为 Wasm 内存格式对应的结构体 m 申请内存
    m = NVM_Alloc(MODULE_STRUCT_SIZE);
    FLA_fill_u8(m, 0x00, MODULE_STRUCT_SIZE);
    //m = (struct Module *)NVM_Alloc(sizeof(struct Module));

    //m->bytes = bytes;
    FLA_write_u32(m + MODULE_STRUCT_BYTE_ADDRESS_OFFSET, bytes);
    //m->byte_count = byte_count;
    FLA_write_u32(m + MODULE_STRUCT_BYTE_COUNT_OFFSET, byte_count);
    //m->block_lookup = (Block **)NVM_Alloc(m->byte_count*sizeof(Block *));//acalloc(m->byte_count, sizeof(Block *), "function->block_lookup");
    //这会不知道有多少个block，先不分配空间
    //tmp32 = NVM_Alloc(byte_count*sizeof(uint32_t));
    //FLA_write_u32(m + MODULE_STRUCT_BLOCK_LOOKUP_ADDRESS_OFFSET, tmp32);
    
    //tmp32 = NVM_Alloc(m->byte_count*sizeof(Block *));
    //FLA_write_u32((uint32_t)&m->block_lookup, tmp32);
    tmp32 = NVM_Alloc(TABLE_STRUCT_SIZE);
    FLA_write_u32(m + MODULE_STRUCT_TABLE_ADDRESS_OFFSET, tmp32);
    // 起始函数索引初始值设置为 -1
    start_function = -1;  



    // 首先读取魔数(magic number)，检查是否正确
    // 注：和其他很多二进制文件（例如 Java 类文件）一样，Wasm 也同样使用魔数来标记其二进制文件类型
    // 所谓魔数，你可以简单地将它理解为具有特定含义的一串数字
    // 一个标准 Wasm 二进制模块文件的头部数据是由具有特殊含义的字节组成的
    // 其中开头的前四个字节为 '（高地址）0x6d 0x73 0x61 0x00（低地址）'，这四个字节对应的 ASCII 字符为 'asm'
    uint32_t magic = FLA_read_u32(bytes + pos);//((uint32_t *) (bytes + pos))[0];
    pos += 4;
    ASSERT(magic == WA_MAGIC, "Wrong module magic 0x%x\n", magic)

    // 然后读取当前 Wasm 二进制文件所使用的 Wasm 标准版本号，检查是否正确
    // 注：紧跟在魔数后面的 4 个字节是用来表示当前 Wasm 二进制文件所使用的 Wasm 标准版本号
    // 目前所有 Wasm 模块该四个字节的值为 '（高地址）0x00 0x00 0x00 0x01（低地址）'，即表示使用的 Wasm 标准版本为 1
    uint32_t version = FLA_read_u32(bytes + pos);//((uint32_t *) (bytes + pos))[0];
    pos += 4;
    ASSERT(version == WA_VERSION, "Wrong module version 0x%x\n", version)

    // 最后根据段 ID 分别解析后面的各个段的内容
    // 和其他二进制格式（例如 Java 类文件）一样，Wasm 二进制格式也是以魔数和版本号开头，
    // 之后就是模块的主体内容，这些内容被分别放在不同的段（Section）中。
    // 一共定义了 12 种段，每种段分配了 ID（从 0 到 11）。除了自定义段之外，其他所有段都最多只能出现一次，且须按照 ID 递增的顺序出现。
    // ID 从 0 到 11 依次有如下 12 个段：
    // 自定义段、类型段、导入段、函数段、表段、内存段、全局段、导出段、起始段、元素段、代码段、数据段
    while (pos < byte_count) {
        // 每个段的第 1 个字节为该段的 ID，用于标记该段的类型
        uint32_t id = read_LEB_unsigned(bytes, &pos, 7);

        // 紧跟在段 ID 后面的 4 个字节用于记录该段所占字节总长度
        uint32_t slen = read_LEB_unsigned(bytes, &pos, 32);

        // 每次解析某个段的数据时，先将当前解析到的位置保存起来，以便后续使用
        uint32_t start_pos = pos;
        DEBUG_PRINTF("Deal with ID = %x.\r\n",id);
        switch (id) {
            case CustomID: {
                // 解析自定义段
                // TODO: 暂不处理自定义段内容，直接跳过
                pos += slen;
                break;
            }
            case TypeID: {
                // 解析类型段
                // 即解析模块中所有函数签名（也叫函数原型）
                // 函数原型示例：(a, b, ...) -> (x, y, ...)

                // 类型段编码格式如下：
                // type_sec: 0x01|byte_count|vec<func_type>

                // 读取类型段中所有函数签名的数量
                //m->type_count = read_LEB_unsigned(bytes, &pos, 32);
                tmp32 = read_LEB_unsigned(bytes, &pos, 32);
                FLA_write_u32(m + MODULE_STRUCT_TYPE_COUNT_OFFSET, tmp32);


                // 为存储类型段中的函数签名申请内存
                //m->types = NVM_Alloc(m->type_count* sizeof(Type));//acalloc(m->type_count, sizeof(Type), "Module->types");
                tmp32 = NVM_Alloc(tmp32 * TYPE_STRUCT_SIZE);
                FLA_write_u32(m + MODULE_STRUCT_TYPE_ADDRESS_OFFSET, tmp32);

                // 遍历解析每个类型 func_type，其编码格式如下：
                // func_type: 0x60|param_count|(param_val)+|return_count|(return_val)+
                for (uint32_t i = 0; i < FLA_read_u32(m + MODULE_STRUCT_TYPE_COUNT_OFFSET); i++) {
                    uint32_t type = FLA_read_u32(m + MODULE_STRUCT_TYPE_ADDRESS_OFFSET) + i*TYPE_STRUCT_SIZE;//&m->types[i];

                    // 函数标记值 FtTag（即 0x60），暂时忽略
                    read_LEB_unsigned(bytes, &pos, 7);

                    // 解析函数参数个数
                    //type->param_count = read_LEB_unsigned(bytes, &pos, 32);
                    tmp32 = read_LEB_unsigned(bytes, &pos, 32);
                    FLA_write_u32(type + TYPE_STRUCT_PARAM_COUNT_OFFSET, tmp32);
                    //type->params = acalloc(type->param_count, sizeof(uint32_t),
                    //                       "type->params");
                    tmp32 = NVM_Alloc(tmp32*sizeof(uint32_t));
                    FLA_write_u32(type + TYPE_STRUCT_PARAM_ADDRESS_OFFSET, tmp32);

                    // 解析函数每个参数的类型
                    for (uint32_t p = 0; p < FLA_read_u32(type + TYPE_STRUCT_PARAM_COUNT_OFFSET); p++) {
                        //type->params[p] = read_LEB_unsigned(bytes, &pos, 32);
                        tmp32 = read_LEB_unsigned(bytes, &pos, 32);
                        FLA_write_u32(FLA_read_u32(type + TYPE_STRUCT_PARAM_ADDRESS_OFFSET) + p*sizeof(uint32_t), tmp32);
                    }

                    // 解析函数返回值个数
                    //type->result_count = read_LEB_unsigned(bytes, &pos, 32);
                    tmp32 = read_LEB_unsigned(bytes, &pos, 32);
                    FLA_write_u32(type + TYPE_STRUCT_RESULT_COUNT_OFFSET, tmp32);

                    //type->results = acalloc(type->result_count, sizeof(uint32_t),
                    //                        "type->results");
                    tmp32 = NVM_Alloc(tmp32 * sizeof(uint32_t));
                    FLA_write_u32(type + TYPE_STRUCT_RESULT_ADDRESS_OFFSET, tmp32);

                    // 解析函数每个返回值的类型
                    for (uint32_t r = 0; r < FLA_read_u32(type + TYPE_STRUCT_RESULT_COUNT_OFFSET); r++) {
                        //type->results[r] = read_LEB_unsigned(bytes, &pos, 32);
                        tmp32 = read_LEB_unsigned(bytes, &pos, 32);
                        FLA_write_u32(FLA_read_u32(type + TYPE_STRUCT_RESULT_COUNT_OFFSET) + r*sizeof(uint32_t), tmp32);
                    }
                
                    // 基于函数签名计算的唯一掩码值
                    //这么写可能不太合适，如果后面又很多64位写的操作，就单独封一个FLA_write_u64
                    tmp64 = get_type_mask(type);
                    FLA_write_u32(type + TYPE_STRUCT_MASK_OFFSET, (tmp64&0xFFFFFFFF00000000)>>32);
                    FLA_write_u32(type + TYPE_STRUCT_MASK_OFFSET + sizeof(uint32_t), (tmp64&0x00000000FFFFFFFF));
                }
                break;
            }
            case ImportID: {
                // 解析导入段
                // 一个模块可以从其他模块导入 4 种类型：函数、表、内存、全局变量
                // 导入项包含 4 个：1.模块名（从哪个模块导入）2.成员名 3. 具体描述

                // 导入段编码格式如下：
                // import_sec: 0x02|byte_count|vec<import>
                // import: module_name|member_name|import_desc
                // import_desc: tag|[type_idx, table_type, mem_type, global_type]

                // 读取导入项数量
                uint32_t import_count = read_LEB_unsigned(bytes, &pos, 32);
                uint32_t import_module_name;
                uint32_t import_field_name;
                // 遍历所有导入项，解析对应数据
                for (uint32_t idx = 0; idx < import_count; idx++) {
                    uint32_t module_len, field_len;

                    // 读取模块名 module_name（从哪个模块导入）
                    char *import_module = read_string(bytes, &pos, &module_len);
                    //import_module_name = bytes

                    // 读取导入项的成员名 member_name
                    char *import_field = read_string(bytes, &pos, &field_len);

                    // 读取导入项类型 tag（四种类型：函数、表、内存、全局变量）
                    uint32_t external_kind = FLA_read_u8(bytes + pos);//bytes[pos++];
                    pos++;

                    uint32_t type_index, fidx;
                    uint8_t global_type, mutability;
                    uint32_t tmpTable; 
                    // 根据不同的导入项类型，读取对应的内容
                    switch (external_kind) {
                        case KIND_FUNCTION:
                            // 读取函数签名索引 type_idx
                            type_index = read_LEB_unsigned(bytes, &pos, 32);
                            break;
                        case KIND_TABLE:
                            // 解析表段中的表 table_type（目前表段只会包含一张表）
                            parse_table_type(m, &pos);
                            break;
                        case KIND_MEMORY:
                            // 解析内存段中内存 mem_type（目前模块只会包含一块内存）
                            parse_memory_type(m, &pos);
                            break;
                        case KIND_GLOBAL:
                            // 先读取全局变量的值类型 global_type
                            global_type = read_LEB_unsigned(bytes, &pos, 7);

                            // 再读取全局变量的可变性
                            mutability = read_LEB_unsigned(bytes, &pos, 1);
                            // TODO: 可变性暂无用处，故先将变量 mutability 标记为无用
                            (void) mutability;
                            break;
                        default:
                            break;
                    }

                    void *val;
                    char *err;//, *sym = malloc(module_len + field_len + 5);

                    do {
                        // 尝试从导入的模块中查找导入项，并将导入项的值赋给 val
                        // 第一个句柄参数为模块名 import_module
                        // 第二个符号参数为成员名 import_field
                        // resolve_sym 函数中，如果从外部模块中找到导入项，则会将导入项的值赋给 val，并返回 true
                        if (resolve_sym(import_module, import_field, &val, &err)) {
                            break;
                        }

                        // 如果未找到，则报错
                        FATAL("Error: %s\n", err)
                    } while (false);

                    //free(sym);

                    // 根据导入项类型，将导入项的值保存到对应的地方
                    switch (external_kind) {
                        case KIND_FUNCTION:
                            // 导入项为导入函数的情况

                            // 获取当前导入函数在本地模块所有函数中的索引
                            //fidx = m->function_count;
                            fidx = FLA_read_u32(m + MODULE_STRUCT_FUNCTION_COUNT_OFFSET);

                            // 本地模块的函数数量和导入函数数量均加 1
                            //m->import_func_count += 1;
                            tmp32 = FLA_read_u32(m + MODULE_STRUCT_IMPORT_FUNC_COUNT_OFFSET);
                            FLA_write_u32(m + MODULE_STRUCT_IMPORT_FUNC_COUNT_OFFSET, tmp32+1);

                            //m->function_count += 1;
                            tmp32 = FLA_read_u32(m + MODULE_STRUCT_FUNCTION_COUNT_OFFSET);
                            FLA_write_u32(m + MODULE_STRUCT_FUNCTION_COUNT_OFFSET, tmp32+1);

                            // 为当前的导入函数对应在本地模块的函数申请内存
                            //m->functions = arecalloc(m->functions, fidx, m->import_func_count, sizeof(Block), "Block(imports)");
                            if (FLA_read_u32(m + MODULE_STRUCT_FUNCTION_ADDRESS_OFFSET) == 0){
                                tmp32 = NVM_Alloc(FLA_read_u32(m + MODULE_STRUCT_IMPORT_FUNC_COUNT_OFFSET) * sizeof(uint32_t));
                            }else {
                                tmp32 = NVM_Alloc(FLA_read_u32(m + MODULE_STRUCT_IMPORT_FUNC_COUNT_OFFSET) * sizeof(uint32_t));
                                FLA_WriteData(tmp32, FLA_read_u32(m + MODULE_STRUCT_FUNCTION_ADDRESS_OFFSET), (FLA_read_u32(m + MODULE_STRUCT_FUNCTION_COUNT_OFFSET)-1) * sizeof(uint32_t));
                                //tmp32 = FLA_read_u32(m + MODULE_STRUCT_FUNCTION_COUNT_OFFSET) + FLA_read_u32(m + MODULE_STRUCT_IMPORT_FUNC_COUNT_OFFSET);
                            }

                            // 获取当前的导入函数对应在本地模块的函数
                            //Block *func = &m->functions[fidx];
                            uint32_t func = NVM_Alloc(BLOCK_STRUCT_SIZE);
                            FLA_write_u32(FLA_read_u32(m + MODULE_STRUCT_FUNCTION_ADDRESS_OFFSET) + fidx*sizeof(uint32_t), func);
                            // 设置【导入函数的导入模块名】为【本地模块中对应函数的导入模块名】
                            //func->import_module = import_module;
                            FLA_write_u32(func + BLOCK_STRUCT_IMPORT_MODULE_OFFSET, import_module);
                            // 设置【导入函数的导入成员名】为【本地模块中对应函数的导入成员名】
                            //func->import_field = import_field;
                            FLA_write_u32(func + BLOCK_STRUCT_IMPORT_FIELD_OFFSET, import_field);
                            // 设置【本地模块中对应函数的指针 func_ptr 】指向【导入函数的实际值】
                            //func->func_ptr = val;
                            FLA_write_u32(func + BLOCK_STRUCT_FUNC_PTR_OFFSET, val);
                            // 设置【导入函数签名】为【本地模块中对应函数的函数签名】
                            //func->type = &m->types[type_index];
                            tmp32 = FLA_read_u32(m + MODULE_STRUCT_TYPE_ADDRESS_OFFSET);
                            FLA_write_u32(func + BLOCK_STRUCT_TYPE_ADDRESS_OFFSET, (tmp32 + type_index*TYPE_STRUCT_SIZE));
                            break;
                        case KIND_TABLE:
                            // 导入项为表的情况
                            /* 暂时不加入对表的处理，试着加以下，以后完善  */
                            // 一个模块只能定义一张表，如果 m->table.entries 不为空，说明已经存在表，则报错
                            //uint32_t tmpTable; 
                            tmpTable = FLA_read_u32(m + MODULE_STRUCT_TABLE_ADDRESS_OFFSET);
                            //ASSERT(!m->table.entries, "More than 1 table not supported\n")
                            Table *tval = val;
                            //m->table.entries = val;
                            FLA_write_u32(tmpTable + TABLE_STRUCT_ENTRIES_ADDRESS_OFFSET, val);
                            // 如果【本地模块的表的当前元素数量】大于【导入表的元素数量上限】，则报错
                            //ASSERT(m->table.cur_size <= tval->max_size, "Imported table is not large enough\n")
                            //m->table.entries = *(uint32_t **) val;
                            FLA_write_u32(tmpTable + TABLE_STRUCT_ENTRIES_ADDRESS_OFFSET, *(uint32_t **) val);
                            // 设置【导入表的当前元素数量】为【本地模块表的当前元素数量】
                            //m->table.cur_size = tval->cur_size;
                            FLA_write_u32(tmpTable + TABLE_STRUCT_CUR_SIZE_OFFSET, tval->cur_size);
                            // 设置【导入表的元素数量限制上限】为【本地模块表的元素数量限制上限】
                            //m->table.max_size = tval->max_size;
                            FLA_write_u32(tmpTable + TABLE_STRUCT_MAX_SIZE_OFFSET, tval->max_size);
                            // 设置【导入表的存储的元素】为【本地模块表的存储的元素】
                            //m->table.entries = tval->entries;
                            FLA_write_u32(tmpTable + TABLE_STRUCT_ENTRIES_ADDRESS_OFFSET, tval->entries);
                            break;
                        case KIND_MEMORY:
                            // 导入项为内存的情况
                            /* 暂时不加入对内存的处理，以后完善
                            // 一个模块只能定义一块内存，如果 m->memory.bytes 不为空，说明已经存在表，则报错
                            ASSERT(!m->memory.bytes, "More than 1 memory not supported\n")
                            Memory *mval = val;
                            // 如果【本地模块的内存的当前页数】大于【导入内存的最大页数】，则报错
                            ASSERT(m->memory.cur_size <= mval->max_size, "Imported memory is not large enough\n")
                            // 设置【导入内存的当前页数】为【本地模块内存的当前页数】
                            m->memory.cur_size = mval->cur_size;
                            // 设置【导入内存的最大页数】为【本地模块内存的最大页数】
                            m->memory.max_size = mval->max_size;
                            // 设置【导入内存的存储的数据】为【本地模块内存的存储的数据】
                            m->memory.bytes = mval->bytes;
                            */
                            break;
                        case KIND_GLOBAL:
                            // 导入项为全局变量的情况

                            // 本地模块的全局变量数量加 1
                            local_global_count += 1;

                            // 为全局变量申请内存，在原有模块本身的全局变量基础上，再添加导入的全局变量对应的全局变量
                            //local_globals = arecalloc(local_globals, local_global_count - 1, local_global_count, sizeof(StackValue), "globals");
                            local_globals = arecalloc(local_globals, local_global_count - 1, local_global_count, sizeof(uint64_t), "globals");
                            // 获取当前的导入全局变量对应在本地模块中的全局变量
                            //StackValue *glob = &local_globals[local_global_count - 1];
                            // 设置【导入全局变量的值类型】为【本地模块中对应全局变量的值类型】
                            // 注：变量的值类型主要为 I32/I64/F32/F64
                            //glob->value_type = global_type;

                            
                            // 根据全局变量的值类型，设置【导入全局变量的值】为【本地模块中对应全局变量的值】
                            switch (global_type) {
                                case I32:
                                case F32:
                                    memcpy(&local_globals[local_global_count - 1], val, 4);
                                    break;
                                case I64:
                                case F64:
                                    memcpy(&local_globals[local_global_count - 1], val, 8);
                                    break;
                                default:
                                    break;
                            }
                            break;
                        default:
                            // 如果导入项为其他类型，则报错
                            FATAL("Import of kind %d not supported\n", external_kind)
                    }
                }
                break;
            }
            case FuncID: {
                // 解析函数段
                // 函数段列出了内部函数的函数签名在所有函数签名中的索引，函数的局部变量和字节码则存在代码段中

                // 函数段编码格式如下：
                // func_sec: 0x03|byte_count|vec<type_idx>

                // 读取函数段所有函数的数量
                //m->function_count += read_LEB_unsigned(bytes, &pos, 32);
                tmp32 = read_LEB_unsigned(bytes, &pos, 32);
                DEBUG_PRINTF("%d functions in this module.\r\n",tmp32);
                FLA_write_u32(m+MODULE_STRUCT_FUNCTION_COUNT_OFFSET, FLA_read_u32(m+MODULE_STRUCT_FUNCTION_COUNT_OFFSET) + tmp32);

                // 为存储函数段中的所有函数申请内存
                uint32_t functions;
                //functions = acalloc(m->function_count, sizeof(Block), "Block(function)");
                functions = NVM_Alloc(FLA_read_u32(m+MODULE_STRUCT_FUNCTION_COUNT_OFFSET)*sizeof(uint32_t));


                // 由于解析了导入段在解析函数段之前，而导入段中可能有导入外部模块函数
                // 因此如果 m->import_func_count 不为 0，则说明已导入外部函数，并存储在了 m->functions 中
                // 所以需要先将存储在了 m->functions 中的导入函数对应数据拷贝到 functions 中
                // 简单来说，就是先将之前解析导入函数所得到的数据，拷贝到新申请的内存中（因为之前申请的内存已不足以存储所有函数的数据）
                //if (m->import_func_count != 0) {
                if (FLA_read_u32(m + MODULE_STRUCT_IMPORT_FUNC_COUNT_OFFSET) != 0){
                    //memcpy(functions, m->functions, sizeof(Block) * m->import_func_count);
                    FLA_WriteData(functions, FLA_read_u32(m+MODULE_STRUCT_FUNCTION_ADDRESS_OFFSET),FLA_read_u32(m + MODULE_STRUCT_IMPORT_FUNC_COUNT_OFFSET)*sizeof(uint32_t));
                }
                //m->functions = functions;
                NVM_Free(FLA_read_u32(m+MODULE_STRUCT_FUNCTION_ADDRESS_OFFSET));
                FLA_write_u32(m+MODULE_STRUCT_FUNCTION_ADDRESS_OFFSET, functions);

                // 遍历每个函数项，读取其对应的函数签名在所有函数签名中的索引，并根据索引获取到函数签名
                //for (uint32_t f = m->import_func_count; f < m->function_count; f++) {
                for (uint32_t f = FLA_read_u32(m + MODULE_STRUCT_IMPORT_FUNC_COUNT_OFFSET); 
                        f < FLA_read_u32(m+MODULE_STRUCT_FUNCTION_COUNT_OFFSET); f++) {
                    uint32_t typeaddr;
                    uint32_t blockaddr; 
                    DEBUG_PRINTF("For function %d in this module.\r\n",f);
                    tmp32 = FLA_read_u32(m + MODULE_STRUCT_FUNCTION_ADDRESS_OFFSET);
                    blockaddr = FLA_read_u32(tmp32 + f*sizeof(uint32_t));
                    if (blockaddr == 0){
                        blockaddr = NVM_Alloc(BLOCK_STRUCT_SIZE);
                        DEBUG_PRINTF("Alloc at %x.\r\n",blockaddr);
                        FLA_write_u32(tmp32 + f*sizeof(uint32_t), blockaddr);
                    }

                    // f 为该函数在所有函数（包括导入函数）中的索引
                    //m->functions[f].fidx = f;
                    FLA_write_u32(blockaddr + BLOCK_STRUCT_FIDX_OFFSET, f);
                    // tidx 为该内部函数的函数签名在所有函数签名中的索引
                    uint32_t tidx = read_LEB_unsigned(bytes, &pos, 32);
                    // 通过索引 tidx 从所有函数签名中获取到具体的函数签名，然后设置为该函数的函数签名
                    //m->functions[f].type = &m->types[tidx];
                    typeaddr = FLA_read_u32(m + MODULE_STRUCT_TYPE_ADDRESS_OFFSET);
                    FLA_write_u32(blockaddr + BLOCK_STRUCT_TYPE_ADDRESS_OFFSET, (typeaddr + tidx*TYPE_STRUCT_SIZE));
                }
                break;
            }
            case TableID: {
                // 解析表段
                // 表段持有一个带有类型的引用数组，比如像函数这种无法作为原始字节存储在模块线性内存中的项目
                // 通过为 Wasm 框架提供一种能安全映射对象的方式，表段可以为 Wasm 提供一部分代码安全性
                // 当代码想要访问表段中引用的数据时，它要向 Wasm 框架请求变种特定索引处的条目，
                // 然后 Wasm 框架会读取存储在这个索引处的地址，并执行相关动作

                // 表段和表项编码格式如下：
                // table_sec: 0x04|byte_count|vec<table_type> # vec 目前长度只能是 1
                // table_type: 0x70|limits
                // limits: flags|min|(max)?

                // 读取表的数量
                uint32_t table_count = read_LEB_unsigned(bytes, &pos, 32);
                // 模块最多只能定义一张表，因此 table_count 必需为 1
                ASSERT(table_count == 1, "More than 1 table not supported\n")

                // 解析表段中的表 table_type（目前模块只会包含一张表）
                parse_table_type(m, &pos);

                // 为存储表中的元素申请内存（在解析元素段时会用到--将元素段中的索引存储到刚申请的内存中）
                //m->table.entries = acalloc(m->table.cur_size, sizeof(uint32_t), "Module->table.entries");
                tmp32 = FLA_read_u32(m + MODULE_STRUCT_TABLE_ADDRESS_OFFSET);
                table_count = NVM_Alloc(FLA_read_u32(tmp32 + TABLE_STRUCT_CUR_SIZE_OFFSET)*sizeof(uint32_t));//临时借用table count
                FLA_write_u32(tmp32 + TABLE_STRUCT_ENTRIES_ADDRESS_OFFSET, table_count);
                break;
            }
            case MemID: {
                // 解析内存段
                // 内存段列出了模块内定义的内存，由于 Wasm 模块不能直接访问设备内存，
                // 实例化模块的环境传入一个 ArrayBuffer，Wasm 模块示例将其用作线性内存。
                // 模块的内存被定义为 Wasm 页，每页 64KB。当环境指定 Wasm 模块可以使用多少内存时，指定的是初始页数，
                // 可能还有一个最大页数。如果模块需要更多内存，可以请求内存增长指定页数。如果指定了最大页数，则框架会防止内存增长超过这一点
                // 如果没有指定最大页数，则内存可以无限增长

                // 内存段和内存类型编码格式如下：
                // mem_sec: 0x05|byte_count|vec<mem_type> # vec 目前长度只能是 1
                // mem_type: limits
                // limits: flags|min|(max)?

                // 读取内存的数量
                uint32_t memory_count = read_LEB_unsigned(bytes, &pos, 32);
                // 模块最多只能定义一块内存，因此 memory_count 必需为 1
                //ASSERT(memory_count == 1, "More than 1 memory not supported\n")

                // 解析内存段中内存 mem_type（目前模块只会包含一块内存）
                parse_memory_type(m, &pos);

                // 为存储内存中的数据申请内存（在解析数据段时会用到--将数据段中的数据存储到刚申请的内存中）
                //m->memory.bytes = acalloc(m->memory.cur_size * PAGE_SIZE, sizeof(uint32_t), "Module->memory.bytes");
                break;
            }
            case GlobalID: {
                // 解析全局段
                // 全局段列出了模块内定义的所有全局变量
                // 每一项包括全局变量的类型（值类型和可变性）以及初始值

                // 全局段和全局项的编码格式如下：
                // global_sec: 0x60|byte_count|vec<global>
                // global: global_type|init_expr
                // global_type: val_type|mut
                // init_expr: (byte)+|0x0B

                // 读取模块中全局变量的数量
                FLA_write_u32(m + MODULE_STRUCT_GLOBAL_ADDRESS_OFFSET, pos);
                uint32_t global_count = read_LEB_unsigned(bytes, &pos, 32);
                
                uint32_t gidx = local_global_count;//FLA_read_u32(m + MODULE_STRUCT_GLOBAL_COUNT_OFFSET);
                //FLA_write_u32(m + MODULE_STRUCT_GLOBAL_COUNT_OFFSET, gidx + global_count);
                local_global_count = gidx + global_count;
                FLA_write_u32(m + MODULE_STRUCT_GLOBAL_COUNT_OFFSET, local_global_count);
                local_globals = arecalloc(local_globals, gidx, gidx + global_count, sizeof(uint64_t), "globals");
                // 遍历全局段中的每一个全局变量项
                for (uint32_t g = 0; g < global_count; g++) {
                    // 先读取全局变量的值类型
                    uint8_t type = read_LEB_unsigned(bytes, &pos, 7);

                    // 再读取全局变量的可变性
                    uint8_t mutability = read_LEB_unsigned(bytes, &pos, 1);
                    // TODO: 可变性暂无用处，故先将变量 mutability 标记为无用
                    (void) mutability;

                    // 先保存当前全局变量的索引
                    //uint32_t gidx = m->global_count;

                    // 全局变量数量加 1
                    //m->global_count += 1;

                    // 由于新增一个全局变量，所以需要重新申请内存，调用 arecalloc 函数在原有内存基础上重新申请内存
                    //local_globals = arecalloc(local_globals, gidx, gidx + 1, sizeof(uint64_t), "globals");

                    // 计算初始化表达式 init_expr，并将计算结果设置为当前全局变量的初始值
                    run_init_expr(m, type, &pos);

                    // 计算初始化表达式 init_expr 也就是栈式虚拟机执行表达式的字节码中的指令流过程，最终操作数栈顶保存的就是表达式的返回值，即计算结果
                    // 将栈顶的值弹出并赋值给当前全局变量即可
                    //m->globals[gidx].value.uint32 = popStack_u32();//m->stack[m->sp--];
                    local_globals[gidx] =  popStack_u32();
                    gidx++;
                }
                pos = start_pos + slen;
                break;
            }
            case ExportID: {
                // 解析导出段
                // 导出段包含模块所有导出成员，主要包含四种：函数、表、内存、全局变量
                // 导出项主要包括两个：1.导出成员名  2.导出描述：1 个字节的类型（0-函数、1-表、2-内存、3-全局变量）+ 导出项在相应段中的索引

                // 导出段编码格式如下：
                // export_sec: 0x07|byte_count|vec<export>
                // export: name|export_desc
                // export_desc: tag|[func_idx, table_idx, mem_idx, global_idx]

                // 读取导出项数量
                uint32_t export_count = read_LEB_unsigned(bytes, &pos, 32);
                FLA_write_u32(m + MODULE_STRUCT_EXPORT_COUNT_OFFSET, export_count);
                tmp32 = NVM_Alloc(export_count * sizeof(uint32_t));
                FLA_write_u32(m + MODULE_STRUCT_EXPORT_ADDRESS_OFFSET, tmp32);
                // 遍历所有导出项，解析对应数据
                for (uint32_t e = 0; e < export_count; e++) {
                    // 读取导出成员名
                    uint32_t tmpExp;
                    char *name;
                    tmpExp = NVM_Alloc(EXPORT_STRUCT_SIZE);
                    FLA_write_u32(tmpExp + EXPORT_STRUCT_EXPORT_NAME_ADDRESS_OFFSET, pos);
                    //没什么用，以后去掉这段
                    name = read_string(bytes, &pos, NULL);
                    afree(name);

                    // 读取导出类型
                    uint32_t external_kind = FLA_read_u8(bytes + pos);//bytes[pos++];
                    pos++;
                    FLA_write_u32(tmpExp + EXPORT_STRUCT_EXTERNAL_KIND_OFFSET, external_kind);
                    // 读取导出项在相应段中的索引
                    uint32_t index = read_LEB_unsigned(bytes, &pos, 32);

                    // 先保存当前导出项的索引
                    //uint32_t eidx = m->export_count;

                    // 导出项数量加 1
                    //m->export_count += 1;

                    // 由于新增一个导出项，所以需要重新申请内存，调用 arecalloc 函数在原有内存基础上重新申请内存
                    //m->exports = arecalloc(m->exports, eidx, m->export_count, sizeof(Export), "exports");

                    // 设置导出项的成员名
                    //m->exports[eidx].export_name = name;

                    // 设置导出项的类型
                    //m->exports[eidx].external_kind = external_kind;

                    // 根据导出项的类型，设置导出项的值
                    switch (external_kind) {
                        case KIND_FUNCTION:
                            // 获取函数并赋给导出项
                            //m->exports[eidx].value = &m->functions[index];
                            tmp32 = FLA_read_u32(m + MODULE_STRUCT_FUNCTION_ADDRESS_OFFSET);
                            FLA_write_u32(tmpExp + EXPORT_STRUCT_VALUE_ADDRESS_OFFSET, FLA_read_u32(tmp32 + index*sizeof(uint32_t)));
                            break;
                        case KIND_TABLE:
                            // 目前 Wasm 版本规定只能定义一张表，所以索引只能为 0
                            ASSERT(index == 0, "Only 1 table in MVP\n")
                            // 获取模块内定义的表并赋给导出项
                            //m->exports[eidx].value = &m->table;
                            FLA_write_u32(tmpExp + EXPORT_STRUCT_VALUE_ADDRESS_OFFSET, FLA_read_u32(m + MODULE_STRUCT_TABLE_ADDRESS_OFFSET));
                            break;
                        case KIND_MEMORY:
                            // 目前 Wasm 版本规定只能定义一个内存，所以索引只能为 0
                            ASSERT(index == 0, "Only 1 memory in MVP\n")
                            // 获取模块内定义的内存并赋给导出项
                            //m->exports[eidx].value = &m->memory;
                            break;
                        case KIND_GLOBAL:
                            // 获取全局变量并赋给导出项
                            //m->exports[eidx].value = &m->globals[index];
                            //如果是导出global的话，这里直接写index吧，暂时先这么设计
                            FLA_write_u32(tmpExp + EXPORT_STRUCT_VALUE_ADDRESS_OFFSET, index);
                            break;
                        default:
                            break;
                    }
                    tmp32 = FLA_read_u32(m + MODULE_STRUCT_EXPORT_ADDRESS_OFFSET);
                    FLA_write_u32(tmp32 + sizeof(uint32_t)*e, tmpExp);
                }

                break;
            }
            case StartID: {
                // 解析起始段
                // 起始段记录了起始函数在本地模块所有函数中索引，而起始函数是在【模块完成初始化后】，【被导出函数可调用之前】自动被调用的函数
                // 可以将起始函数视为一种初始化全局变量或内存的函数，且函数必须处于被模块内部，不能是从外部导入的
                // 起始函数的作用有两个：
                // 1. 在模块加载后进行初始化工作
                // 2. 将模块变成可执行文件

                // 起始段的编码格式如下：
                // start_sec: 0x08|byte_count|func_idx
                start_function = read_LEB_unsigned(bytes, &pos, 32) ;
                break;
            }
            case ElemID: {
                // 解析元素段
                // 元素段用于存放表初始化数据
                // 元素项包含三部分：1.表索引（初始化哪张表）2.表内偏移量（从哪开始初始化） 3. 函数索引列表（给定的初始化数据）

                // 元素段编码格式如下：
                // elem_sec: 0x09|byte_count|vec<elem>
                // elem: table_idx|offset_expr|vec<func_id>

                // 读取元素数量
                uint32_t elem_count = read_LEB_unsigned(bytes, &pos, 32);

                // 依次对表中每个元素进行初始化
                for (uint32_t c = 0; c < elem_count; c++) {
                    // 读取表索引 table_idx（即初始化哪张表）
                    uint32_t index = read_LEB_unsigned(bytes, &pos, 32);
                    // 目前 Wasm 版本规定一个模块只能定义一张表，所以 index 只能为 0
                    ASSERT(index == 0, "Only 1 default table in MVP\n")

                    // 计算初始化表达式 offset_expr，并将计算结果设置为当前表内偏移量 offset
                    run_init_expr(m, I32, &pos);

                    // 计算初始化表达式 offset_expr 也就是栈式虚拟机执行表达式的字节码中的指令流过程，最终操作数栈顶保存的就是表达式的返回值，即计算结果
                    // 将栈顶的值弹出并赋值给当前表内偏移量 offset
                    uint32_t offset = popStack_u32();//m->stack[m->sp--].value.uint32;

                    // 函数索引列表（即给定的元素初始化数据）
                    uint32_t num_elem = read_LEB_unsigned(bytes, &pos, 32);
                    uint32_t tmpTable = FLA_read_u32(m + MODULE_STRUCT_TABLE_ADDRESS_OFFSET);
                    // 遍历函数索引列表，将列表中的函数索引设置为元素的初始值
                    for (uint32_t n = 0; n < num_elem; n++) {
                        //m->table.entries[offset + n] = read_LEB_unsigned(bytes, &pos, 32);
                        tmp32 = FLA_read_u32(tmpTable + TABLE_STRUCT_ENTRIES_ADDRESS_OFFSET);
                        FLA_write_u32(tmp32 + (offset + n)*sizeof(uint32_t), read_LEB_unsigned(bytes, &pos, 32));
                    }
                }
                pos = start_pos + slen;
                break;
            }
            case CodeID: {
                // 解析代码段
                // 代码段用于存放函数的字节码和局部变量，是 Wasm 二进制模块的核心，其他段存放的都是辅助信息
                // 为了节约空间，局部变量的信息是被压缩的：即连续多个相同类型的局部变量会被统一记录变量数量和类型

                // 代码段编码格式如下：
                // code_sec: 0xoA|byte_count|vec<code>
                // code: byte_count|vec<locals>|expr
                // locals: local_count|val_type

                // 读取代码段中的代码项的数量
                uint32_t code_count = read_LEB_unsigned(bytes, &pos, 32);
                DEBUG_PRINTF("code count is %x.\r\n",code_count);
                // 声明局部变量的值类型
                uint8_t val_type;

                // 遍历代码段中的每个代码项，解析对应数据
                for (uint32_t c = 0; c < code_count; c++) {
                    // 获取代码项
                    //Block *function = &m->functions[m->import_func_count + c];
                    uint32_t blockAddr;
                    tmp32 = FLA_read_u32(m+MODULE_STRUCT_FUNCTION_ADDRESS_OFFSET);
                    blockAddr = FLA_read_u32(tmp32 + (FLA_read_u32(MODULE_STRUCT_IMPORT_FUNC_COUNT_OFFSET) + c )*sizeof(uint32_t));


                    // 读取代码项所占字节数（暂用 4 个字节）
                    uint32_t code_size = read_LEB_unsigned(bytes, &pos, 32);

                    // 保存当前位置为代码项的起始位置（除去前面的表示代码项目长度的 4 字节）
                    uint32_t payload_start = pos;

                    // 读取 locals 数量（注：相同类型的局部变量算一个 locals）
                    uint32_t local_type_count = read_LEB_unsigned(bytes, &pos, 32);

                    uint32_t save_pos, lidx, lecount, local_count;

                    // 接下来需要对局部变量的相关字节进行两次遍历，所以先保存当前位置，方便第二次遍历前恢复位置
                    save_pos = pos;

                    // 将代码项的局部变量数量初始化为 0
                    //function->local_count = 0;
                    local_count = 0;

                    // 第一次遍历所有的 locals，目的是统计代码项的局部变量数量，将所有 locals 所包含的变量数量相加即可
                    // 注：相同类型的局部变量算一个 locals
                    for (uint32_t l = 0; l < local_type_count; l++) {
                        // 读取单个 locals 所包含的变量数量
                        lecount = read_LEB_unsigned(bytes, &pos, 32);

                        // 累加 locals 所对应的局部变量的数量
                        local_count += lecount;

                        // 局部变量的数量后面接的是局部变量的类型，暂时不需要，标记为无用
                        val_type = read_LEB_unsigned(bytes, &pos, 7);
                        (void) val_type;
                    }
                    FLA_write_u32(blockAddr + BLOCK_STRUCT_LOCAL_COUNT_OFFSET, local_count);

                    // 为保存函数局部变量的值类型的 function->locals 数组申请内存
                    //function->locals = acalloc(function->local_count, sizeof(uint32_t), "function->locals");
                    tmp32 = NVM_Alloc(local_count*sizeof(uint32_t));
                    FLA_write_u32(blockAddr + BLOCK_STRUCT_LOCAL_ADDRESS_OFFSET, tmp32);

                    // 恢复之前的位置，重新遍历所有的 locals
                    pos = save_pos;

                    // 将局部变量的索引初始化为 0
                    lidx = 0;

                    // 第二次遍历所有的 locals，目的是所有的代码项中所有的局部变量设置值类型
                    for (uint32_t l = 0; l < local_type_count; l++) {
                        // 读取单个 locals 所包含的变量数量
                        lecount = read_LEB_unsigned(bytes, &pos, 32);

                        // 读取单个 locals 的值类型
                        val_type = read_LEB_unsigned(bytes, &pos, 7);

                        // 为该 locals 所对应的每一个变量设置值类型（注：相同类型的局部变量算一个 locals）
                        for (uint32_t n = 0; n < lecount; n++) {
                            //function->locals[lidx++] = val_type;
                            FLA_write_u32(tmp32 + lidx*sizeof(uint32_t), val_type);
                        }
                    }

                    // 在代码项中，紧跟在局部变量后面的就是代码项的字节码部分

                    // 先读取单个代码项的字节码部分【起始地址】（即局部变量部分的后一个字节）
                    //function->start_addr = pos;
                    FLA_write_u32(blockAddr + BLOCK_STRUCT_START_ADDRESS_OFFSET, pos);

                    // 然后读取单个代码项的字节码部分【结束地址】，同时作为字节码部分【跳转地址】
                    //function->end_addr = payload_start + code_size - 1;
                    //DEBUG_PRINTF("payload_start = %d.\r\n",payload_start);
                    //DEBUG_PRINTF("code_size = %d.\r\n",code_size);
                    FLA_write_u32(blockAddr + BLOCK_STRUCT_END_ADDRESS_OFFSET, payload_start + code_size - 1);
                    //function->br_addr = function->end_addr;
                    FLA_write_u32(blockAddr + BLOCK_STRUCT_BR_ADDRESS_OFFSET, FLA_read_u32(blockAddr + BLOCK_STRUCT_END_ADDRESS_OFFSET));

                    // 代码项的字节码部分必须以 0x0b 结尾
                    //ASSERT(bytes[function->end_addr] == 0x0b, "Code section did not end with 0x0b\n")
                    //DEBUG_PRINTF("Block end with %02x, at address %d\r\n",FLA_read_u8(bytes + FLA_read_u32(blockAddr + BLOCK_STRUCT_END_ADDRESS_OFFSET)),FLA_read_u32(blockAddr + BLOCK_STRUCT_END_ADDRESS_OFFSET));
                    ASSERT(FLA_read_u8(bytes + FLA_read_u32(blockAddr + BLOCK_STRUCT_END_ADDRESS_OFFSET))== 0x0b, "Code section did not end with 0x0b\n")

                    // 更新当前的地址为当前代码项的【结束地址】（即代码项的字节码部分【结束地址】）加 1，以便遍历下一个代码项
                    //pos = function->end_addr + 1;
                    pos = FLA_read_u32(blockAddr + BLOCK_STRUCT_END_ADDRESS_OFFSET) + 1;
                }
                break;
            }
            case DataID: {
                // 解析数据段
                // 数据段用于存放内存的初始化数据
                // 元素项包含三部分：1.内存索引（初始化哪块内存）2. 内存偏移量（从哪里开始初始化）3. 初始化数据

                // 数据段编码格式如下：
                // data_sec: 0x09|byte_count|vec<data>
                // data: mem_idx|offset_expr|vec<byte>

                // 读取数据数量
                uint32_t mem_count = read_LEB_unsigned(bytes, &pos, 32);

                // 依次对内存中每个部分进行初始化
                for (uint32_t s = 0; s < mem_count; s++) {
                    // 读取内存索引 mem_idx（即初始化哪块内存）
                    uint32_t index = read_LEB_unsigned(bytes, &pos, 32);
                    // 目前 Wasm 版本规定一个模块只能定义一块内存，所以 index 只能为 0
                    ASSERT(index == 0, "Only 1 default memory in MVP\n")

                    // 计算初始化表达式 offset_expr，并将计算结果设置为当前内存偏移量 offset
                    run_init_expr(m, I32, &pos);

                    // 计算初始化表达式 offset_expr 也就是栈式虚拟机执行表达式的字节码中的指令流过程，最终操作数栈顶保存的就是表达式的返回值，即计算结果
                    // 将栈顶的值弹出并赋值给当前内存偏移量 offset
                    uint32_t offset = popStack_u32();//m->stack[m->sp--].value.uint32;

                    // 读取初始化数据所占内存大小
                    uint32_t size = read_LEB_unsigned(bytes, &pos, 32);

                    // 将写在二进制文件中的初始化数据拷贝到指定偏移量的内存中
                    //memcpy(m->memory.bytes + offset, bytes + pos, size);
                    pos += size;
                }
                break;
            }
            default: {
                // 如果没有匹配到任何段，则只需 pos 增加相应值即可
                pos += slen;
                // 如果不是上面 0 到 11 ID，则报错
                FATAL("Section %d unimplemented\n", id)
            }
        }
    }

    // 收集所有本地模块定义的函数中 Block_/Loop/If 控制块的相关信息，例如起始地址、结束地址、跳转地址、控制块类型等，
    // 便于后续虚拟机解释执行指令时可以借助这些信息
    find_blocks(m);

    // 起始函数 m->start_function 是在【模块完成初始化后】，【被导出函数可调用之前】自动被调用的函数
    // 可以将起始函数视为一种初始化全局变量或内存的函数，且起始函数必须处于本地模块内部，不能是从外部导入的函数

    // m->start_function 初始赋值为 -1
    // 在解析 Wasm 二进制文件中的起始段时，start_function 会被赋值为起始段中保存的起始函数索引（在本地模块所有函数的索引）
    // 所以 m->start_function 不为 -1，说明本地模块存在起始函数，
    // 需要在本地模块已完成初始化后，且本地模块的导出函数被调用之前，执行本地模块的起始函数
    if (start_function != -1) {
        // 保存起始函数索引到 fidx
        uint32_t fidx = start_function;
        bool result;

        // 起始函数必须处于本地模块内部，不能是从外部导入的函数
        // 注：从外部模块导入的函数在本地模块的所有函数中的前部分，可参考上面解析 Wasm 二进制文件导入段中处理外部模块导入函数的逻辑
        ASSERT(fidx >= FLA_read_u32(m + MODULE_STRUCT_IMPORT_FUNC_COUNT_OFFSET), "Start function should be local function of native module\n")

        // 调用 Wasm 模块的起始函数
        result = invoke(m, fidx);

        // 虚拟机在执行起始函数的字节码中的指令，如果遇到错误会返回 false，否则顺利执行完成后会返回 true
        // 如果为 false，则将运行时（虚拟机执行指令过程）收集的异常信息打印出来
        if (!result) {
            FATAL("Exception: %s\n", exception)
        }
        
    }

    FLA_write_u32(m + MODULE_STRUCT_START_FUNCTION_ADDRESS_OFFSET, start_function);


    if (local_globals != 0){
        tmp32 = NVM_Alloc(local_global_count * sizeof(uint32_t));
        FLA_write_u32(m + MODULE_STRUCT_GLOBAL_VALUES_ADDRESS_OFFSET, tmp32);
        for (uint32_t i = 0; i < local_global_count; i++){
            FLA_write_u32(tmp32 + i*sizeof(uint32_t), (uint32_t)local_globals[i]);
        }
    }
    return m;
}
