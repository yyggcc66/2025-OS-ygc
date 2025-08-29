#include "co.h"
#include <stdlib.h>
#include <setjmp.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

// 常量定义 - 堆栈大小设置为64KB
#define KILOBYTE               1024
#define CO_STACK_SIZE          (64 * KILOBYTE)

// 跳转返回值常量 - 用于区分首次setjmp和longjmp返回
#define CO_JMP_RET             1

/**
 * @brief 协程状态枚举
 * 描述协程生命周期中的不同状态
 */
typedef enum {
    CO_STATUS_NEW,        // 新创建，尚未执行
    CO_STATUS_RUNNING,    // 正在执行或已执行过被切换出去
    CO_STATUS_WAITING,    // 等待其他协程完成
    CO_STATUS_DEAD        // 执行结束，但资源未释放
} CoStatus;

/**
 * @brief 协程结构体定义
 * 包含协程运行所需的所有信息
 */
struct co {
    const char *name;         // 协程名称
    void (*func)(void *);     // 协程入口函数
    void *arg;                // 传递给入口函数的参数
    
    CoStatus status;          // 协程当前状态
    struct co *waiter;        // 等待当前协程的协程（如有）
    jmp_buf context;          // 保存协程上下文（寄存器状态）
    uint8_t stack[CO_STACK_SIZE];  // 协程私有堆栈
};

// 全局变量 - 当前正在运行的协程
static struct co *current_co = NULL;

/**
 * @brief 协程链表节点
 * 用于维护就绪协程队列
 */
typedef struct CoNode {
    struct co *coroutine;     // 指向协程结构体
    struct CoNode *prev;      // 前一个节点
    struct CoNode *next;      // 后一个节点
} CoNode;

// 全局变量 - 协程队列头节点
static CoNode *co_queue = NULL;

/**
 * @brief 汇编辅助函数 - 切换到新的堆栈并调用函数
 * 保存当前寄存器状态，设置新堆栈，调用指定函数
 * @param sp 新堆栈指针
 * @param entry 要调用的函数入口
 * @param arg 传递给函数的参数
 */
static inline void stack_switch_call(void *sp, void *entry, void *arg) {
    asm volatile(
#if __x86_64__
        // x86_64架构: 保存寄存器到堆栈，设置新堆栈，调用函数
        "movq %%rax, 0(%0); movq %%rdi, 8(%0); movq %%rsi, 16(%0);"
        "movq %%rdx, 24(%0); movq %%rcx, 32(%0);"
        "movq %0, %%rsp; movq %2, %%rdi; call *%1"
#else
        // x86架构: 保存寄存器到堆栈，设置新堆栈，调用函数
        "movl %%eax, 0(%0); movl %%edi, 4(%0); movl %%esi, 8(%0);"
        "movl %%edx, 12(%0); movl %%ecx, 16(%0);"
        "movl %0, %%esp; movl %2, 0(%0); call *%1"
#endif
        :
        : "b"((uintptr_t)sp - (sizeof(uintptr_t) * 6)), 
          "d"((uintptr_t)entry), 
          "a"((uintptr_t)arg)
    );
}

/**
 * @brief 汇编辅助函数 - 恢复寄存器状态
 * 从堆栈中恢复之前保存的寄存器值
 */
static inline void restore_registers() {
    asm volatile(
#if __x86_64__
        // x86_64架构: 从堆栈恢复寄存器
        "movq 0(%%rsp), %%rax; movq 8(%%rsp), %%rdi; movq 16(%%rsp), %%rsi;"
        "movq 24(%%rsp), %%rdx; movq 32(%%rsp), %%rcx"
#else
        // x86架构: 从堆栈恢复寄存器
        "movl 0(%%esp), %%eax; movl 4(%%esp), %%edi; movl 8(%%esp), %%esi;"
        "movl 12(%%esp), %%edx; movl 16(%%esp), %%ecx"
#endif
        :
        :
    );
}

/**
 * @brief 将新协程插入到协程队列
 * @param new_co 要插入的新协程
 */
static void co_queue_insert(struct co *new_co) {
    // 为新协程创建队列节点
    CoNode *node = (CoNode *)malloc(sizeof(CoNode));
    assert(node != NULL && "内存分配失败: 创建协程节点");
    
    node->coroutine = new_co;
    
    // 处理队列空的情况
    if (co_queue == NULL) {
        node->prev = node;
        node->next = node;
        co_queue = node;
    } else {
        // 插入到队列头部之前（环形链表）
        node->prev = co_queue->prev;
        node->next = co_queue;
        node->prev->next = node;
        node->next->prev = node;
    }
}

/**
 * @brief 从协程队列移除头部节点
 * @return 被移除的节点，如果队列为空则返回NULL
 */
static CoNode *co_queue_remove() {
    if (co_queue == NULL) {
        return NULL;
    }
    
    CoNode *victim = co_queue;
    
    // 处理队列中只剩一个节点的情况
    if (co_queue->next == co_queue) {
        co_queue = NULL;
    } else {
        // 调整队列指针
        co_queue = co_queue->next;
        co_queue->prev = victim->prev;
        co_queue->prev->next = co_queue;
    }
    
    return victim;
}

/**
 * @brief 创建并初始化一个新的协程
 * @param name 协程名称
 * @param func 协程入口函数
 * @param arg 传递给入口函数的参数
 * @return 指向新创建的协程的指针
 */
struct co *co_start(const char *name, void (*func)(void *), void *arg) {
    // 分配协程结构体内存
    struct co *new_co = (struct co *)malloc(sizeof(struct co));
    assert(new_co != NULL && "内存分配失败: 创建协程结构体");
    
    // 初始化协程属性
    new_co->name = name;
    new_co->func = func;
    new_co->arg = arg;
    new_co->status = CO_STATUS_NEW;
    new_co->waiter = NULL;
    
    // 将新协程加入调度队列
    co_queue_insert(new_co);
    
    return new_co;
}

/**
 * @brief 等待指定协程完成
 * @param coroutine 要等待的协程
 */
void co_wait(struct co *coroutine) {
    // 如果被等待的协程还未结束，则当前协程进入等待状态
    if (coroutine->status != CO_STATUS_DEAD) {
        // 记录等待关系
        coroutine->waiter = current_co;
        current_co->status = CO_STATUS_WAITING;
        
        // 让出CPU，切换到其他协程
        co_yield();
    }
    
    // 找到并移除已结束的协程
    if (co_queue != NULL) {
        CoNode *current_node = co_queue;
        do {
            if (current_node->coroutine == coroutine) {
                co_queue = current_node;  // 设置当前节点为新的队列头
                break;
            }
            current_node = current_node->next;
        } while (current_node != co_queue);
    }
    
    // 释放协程资源
    assert(co_queue->coroutine == coroutine && "等待的协程不在队列中");
    free(coroutine);
    
    CoNode *node = co_queue_remove();
    assert(node != NULL && "移除的协程节点为空");
    free(node);
}

/**
 * @brief 协程让出CPU，切换到其他就绪协程
 */
void co_yield(void) {
    // 保存当前协程的上下文
    int jump_result = setjmp(current_co->context);
    
    // 首次调用setjmp返回0，表示需要进行协程切换
    if (jump_result == 0) {
        CoNode *start_node = co_queue;
        CoNode *next_node = NULL;
        
        // 查找下一个就绪的协程（状态为NEW或RUNNING）
        do {
            co_queue = co_queue->next;
            if (co_queue->coroutine->status == CO_STATUS_RUNNING || 
                co_queue->coroutine->status == CO_STATUS_NEW) {
                next_node = co_queue;
                break;
            }
        } while (co_queue != start_node);  // 循环整个队列
        
        // 检查是否所有协程都已完成
        if (next_node == NULL) {
            exit(0);  // 所有协程结束，退出程序
        }
        
        // 切换到选中的协程
        current_co = next_node->coroutine;
        
        if (current_co->status == CO_STATUS_RUNNING) {
            // 恢复已运行过的协程的上下文
            longjmp(current_co->context, CO_JMP_RET);
        } 
        else if (current_co->status == CO_STATUS_NEW) {
            // 初始化并运行新协程
            current_co->status = CO_STATUS_RUNNING;
            
            // 切换到新协程的堆栈并调用入口函数
            stack_switch_call(
                current_co->stack + CO_STACK_SIZE, 
                current_co->func, 
                current_co->arg
            );
            
            // 协程函数返回后恢复寄存器
            restore_registers();
            
            // 标记协程为已结束
            current_co->status = CO_STATUS_DEAD;
            
            // 如果有等待该协程的协程，唤醒它
            if (current_co->waiter != NULL) {
                current_co->waiter->status = CO_STATUS_RUNNING;
            }
            
            // 继续调度其他协程
            co_yield();
        }
    } 
    else {
        // 从其他协程切换回来，验证状态
        assert(jump_result == CO_JMP_RET && 
               current_co->status == CO_STATUS_RUNNING && 
               "协程切换状态错误");
        return;
    }
}

/**
 * @brief 构造函数 - 在程序启动时初始化主协程
 * 编译器扩展，在main函数执行前调用
 */
static __attribute__((constructor)) void co_initialize() {
    // 创建主协程作为程序的初始协程
    current_co = co_start("main", NULL, NULL);
    current_co->status = CO_STATUS_RUNNING;
}

/**
 * @brief 析构函数 - 在程序退出时清理资源
 * 编译器扩展，在main函数执行后调用
 */
static __attribute__((destructor)) void co_cleanup() {
    // 释放所有剩余的协程节点和结构体
    while (co_queue != NULL) {
        CoNode *node = co_queue_remove();
        if (node != NULL) {
            free(node->coroutine);
            free(node);
        }
    }
}
