#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <random>
#include <string>
#include <chrono>

using namespace std;

// ===================== 系统配置常量 =====================
// 模拟系统资源约束
constexpr int MAX_CPU_COUNT = 8;                    // 最大处理器数量
constexpr long long MAX_MEMORY_CAPACITY = 4294967296LL; // 4GB，最大内存容量（字节）
constexpr long long MIN_MEMORY_CAPACITY = 67108864LL;   // 64MB，最小内存容量（字节）
constexpr int TASKS_PER_PROCESSOR = 10;             // 每个处理器执行的任务数量

// ===================== 全局系统状态 =====================
int active_processor_count = 0;                     // 当前活跃的处理器数量
long long total_memory_capacity = 0;                // 系统总内存容量（字节）
const int INT_MAX=2147483647;

// ===================== 内存管理数据结构 =====================
/**
 * @brief 内存块描述符
 * 
 * 描述系统中已分配的一块连续内存区域
 */
struct MemoryBlockDescriptor {
    long long base_address;  // 内存块起始地址
    long long block_size;    // 内存块大小（字节）
    
    /**
     * @brief 构造函数
     * @param start 起始地址
     * @param size 块大小
     */
    MemoryBlockDescriptor(long long start, long long size) 
        : base_address(start), block_size(size) {}
    
    /**
     * @brief 默认构造函数
     */
    MemoryBlockDescriptor() : base_address(0), block_size(0) {}
    
    /**
     * @brief 重载相等运算符
     * @param other 比较对象
     * @return 是否相同
     */
    bool operator==(const MemoryBlockDescriptor& other) const {
        return base_address == other.base_address && block_size == other.block_size;
    }
};

// 已分配内存块记录表
vector<MemoryBlockDescriptor> allocated_memory_blocks;

// 内存管理互斥锁，确保内存操作的原子性
mutex memory_management_mutex;

// ===================== 内存管理函数 =====================

/**
 * @brief 检查内存区域是否可用
 * 
 * 检查从指定起始地址开始指定大小的内存区域是否与已分配区域重叠
 * 
 * @param proposed_base 提议的起始地址
 * @param proposed_size 提议的内存大小
 * @return true 内存区域可用
 * @return false 内存区域已被占用
 */
bool is_memory_region_available(long long proposed_base, long long proposed_size) {
    // 计算提议区域的结束地址（包含边界）
    long long proposed_end = proposed_base + proposed_size - 1;
    
    // 遍历所有已分配的内存块
    for (const auto& allocated_block : allocated_memory_blocks) {
        long long allocated_start = allocated_block.base_address;
        long long allocated_end = allocated_block.base_address + allocated_block.block_size - 1;
        
        // 检查地址范围是否有重叠
        if (proposed_end >= allocated_start && proposed_base <= allocated_end) {
            return false; // 发现重叠，区域不可用
        }
    }
    
    return true; // 无重叠，区域可用
}

/**
 * @brief 内存分配函数
 * 
 * 按照对齐要求分配指定大小的内存块
 * 
 * @param requested_size 请求的内存大小
 * @return MemoryBlockDescriptor* 分配的内存块描述符指针，失败返回nullptr
 */
MemoryBlockDescriptor* allocate_memory(long long requested_size) {
    // 自动加锁，函数返回时自动释放
    lock_guard<mutex> lock_guard(memory_management_mutex);
    
    // 检查请求大小是否超过单次分配上限（16MB）
    constexpr long long MAX_SINGLE_ALLOCATION = 16LL * 1024 * 1024;
    if (requested_size > MAX_SINGLE_ALLOCATION) {
        return nullptr;
    }
    
    // 计算对齐要求：找到不小于requested_size的最小2的幂
    long long alignment_requirement = 1;
    while (alignment_requirement < requested_size) {
        alignment_requirement *= 2;
        // 由于16MB限制，无需担心溢出
    }
    
    // 按照对齐要求搜索可用内存区域
    for (long long candidate_address = alignment_requirement; 
         candidate_address <= total_memory_capacity - requested_size; 
         candidate_address += alignment_requirement) {
        
        if (is_memory_region_available(candidate_address, requested_size)) {
            // 找到可用区域，创建内存块描述符
            MemoryBlockDescriptor* new_block = new MemoryBlockDescriptor(candidate_address, requested_size);
            allocated_memory_blocks.push_back(*new_block); // 记录分配信息
            
            return new_block;
        }
    }
    
    // 内存不足，分配失败
    return nullptr;
}

/**
 * @brief 内存释放函数
 * 
 * 释放之前分配的内存块
 * 
 * @param block_to_free 要释放的内存块描述符指针
 */
void deallocate_memory(MemoryBlockDescriptor* block_to_free) {
    if (block_to_free == nullptr) {
        return; // 空指针检查
    }
    
    // 自动加锁，确保线程安全
    lock_guard<mutex> lock_guard(memory_management_mutex);
    
    // 在已分配记录中查找并移除对应块
    for (auto iterator = allocated_memory_blocks.begin(); 
         iterator != allocated_memory_blocks.end(); 
         ++iterator) {
        
        if (*iterator == *block_to_free) {
            allocated_memory_blocks.erase(iterator);
            break;
        }
    }
    
    // 释放描述符内存
    delete block_to_free;
}

// ===================== 工具函数 =====================

/**
 * @brief 线程安全的随机数生成器
 * 
 * 每个线程有独立的随机数生成器，避免竞争条件
 * 
 * @return int 随机正整数
 */
int generate_thread_safe_random() {
    // thread_local确保每个线程有独立的生成器实例
    thread_local mt19937 random_engine(random_device{}());
    uniform_int_distribution<int> distribution(0, INT_MAX);
    return distribution(random_engine);
}

// 控制台输出互斥锁，避免多线程输出混乱
mutex console_output_mutex;

/**
 * @brief 线程安全的控制台输出函数
 * 
 * @param message 要输出的消息
 */
void thread_safe_print(const string& message) {
    lock_guard<mutex> lock_guard(console_output_mutex);
    cout << message << endl;
}

// ===================== 处理器工作函数 =====================

/**
 * @brief 处理器工作例程
 * 
 * 模拟处理器执行内存分配和释放任务
 * 
 * @param processor_id 处理器标识符
 */
void processor_work_routine(int processor_id) {
    string log_message;
    
    // 记录处理器启动
    log_message = "处理器 [" + to_string(processor_id) + "] 开始执行任务";
    thread_safe_print(log_message);
    
    // 执行指定数量的任务
    for (int task_index = 0; task_index < TASKS_PER_PROCESSOR; ++task_index) {
        // 生成随机数决定内存分配类型
        int random_value = generate_thread_safe_random() % 100 + 1;
        long long allocation_size = 0;
        
        // 根据概率分布确定内存大小
        if (random_value <= 60) {
            // 60%概率：小内存分配 (1-128字节)
            allocation_size = generate_thread_safe_random() % 128 + 1;
        } else if (random_value <= 95) {
            // 35%概率：中等内存分配 (4KB)
            allocation_size = 4LL * 1024;
        } else {
            // 5%概率：大内存分配 (4KB-16MB)
            long long min_size = 4LL * 1024;
            long long max_size = 16LL * 1024 * 1024;
            allocation_size = generate_thread_safe_random() % (max_size - min_size + 1) + min_size;
        }
        
        // 尝试内存分配
        log_message = "处理器 [" + to_string(processor_id) + "] 请求分配 " + 
                     to_string(allocation_size) + " 字节内存";
        thread_safe_print(log_message);
        
        MemoryBlockDescriptor* memory_block = allocate_memory(allocation_size);
        
        if (memory_block == nullptr) {
            log_message = "处理器 [" + to_string(processor_id) + "] 内存分配失败";
            thread_safe_print(log_message);
            continue;
        }
        
        log_message = "处理器 [" + to_string(processor_id) + "] 分配成功: " +
                     "起始地址=" + to_string(memory_block->base_address) + 
                     ", 大小=" + to_string(memory_block->block_size);
        thread_safe_print(log_message);
        
        // 模拟内存使用时间（1-5秒）
        int usage_duration = generate_thread_safe_random() % 5 + 1;
        this_thread::sleep_for(chrono::seconds(usage_duration));
        
        // 释放内存
        deallocate_memory(memory_block);
        log_message = "处理器 [" + to_string(processor_id) + "] 已释放内存块";
        thread_safe_print(log_message);
    }
    
    // 记录处理器完成
    log_message = "处理器 [" + to_string(processor_id) + "] 完成任务";
    thread_safe_print(log_message);
}

// ===================== 主函数 =====================

/**
 * @brief 程序入口点
 * 
 * 模拟多处理器环境下的内存分配测试
 * 
 * @return int 程序退出码
 */
int main() {
    // 初始化随机系统配置
    active_processor_count = generate_thread_safe_random() % MAX_CPU_COUNT + 1;
    cout << "系统配置: " << active_processor_count << " 个处理器" << endl;
    
    total_memory_capacity = generate_thread_safe_random() % 
                          (MAX_MEMORY_CAPACITY - MIN_MEMORY_CAPACITY + 1) + 
                          MIN_MEMORY_CAPACITY;
    cout << "内存容量: " << total_memory_capacity << " 字节 (" 
         << static_cast<double>(total_memory_capacity) / (1024 * 1024) << " MB)" << endl;
    
    // 创建处理器线程
    vector<thread> processor_threads;
    for (int i = 0; i < active_processor_count; ++i) {
        processor_threads.emplace_back(processor_work_routine, i);
    }
    
    // 等待所有处理器完成任务
    for (auto& thread : processor_threads) {
        thread.join();
    }
    
    return 0;
}