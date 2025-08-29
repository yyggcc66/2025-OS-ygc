#include "L2.h"

// L2 相对于 L1 进行了架构重构和优化，提升了代码结构和可维护性

// 线程安全的随机数生成函数，避免多线程环境下的竞争条件
// 使用 thread_local 确保每个线程有独立的随机数生成器实例
int GetRandomIntThreadSafe() {
    thread_local mt19937 gen(random_device{}());  // 每个线程独立的梅森旋转算法生成器
    uniform_int_distribution<int> dist(0, 2147483647);  // 均匀分布，范围0到INT_MAX
    return dist(gen);  // 生成并返回随机数
}

// ===================== Memory 类成员函数实现 =====================

// 内存系统初始化函数
// 负责设置内存页面数量并初始化所有页面的状态
bool Memory::Init() {
    // 随机生成页面数量，范围在MIN_PAGES到MAX_PAGES之间
    int SizeOfStack = 0;
    SizeOfStack = GetRandomIntThreadSafe() % (MAX_PAGES - MIN_PAGES + 1) + MIN_PAGES;

    string Console;  // 日志消息缓冲区

    // 输出初始化开始日志
    Console = "内存初始化开始。";
    os.Print(Console);
    
    // 输出详细的内存配置信息到控制台
    cout << "随机的页面数量为：" << to_string(SizeOfStack) << " 页，约 " 
         << to_string((float)SizeOfStack * PAGE_SIZE / 1048576) << " MB" << endl;
    
    // 初始化所有内存页面
    for (int i = 0; i < SizeOfStack; i++) {
        Page p;  // 创建新的页面对象
        p.PageNumber = i;  // 设置页面编号
        p.OccupiedTask = nullptr;  // 初始状态为空闲
        Pages.push_back(p);  // 添加到页面列表
    }

    // 输出初始化完成日志
    Console = "内存初始化完成。";
    os.Print(Console);

    return 1;  // 返回成功状态
}

// 检查指定内存区域是否可用（未被占用）
// 参数：Start - 起始地址，Size - 内存大小
// 返回值：true-可用，false-已被占用
bool Memory::CheckMemory(long long Start, long long Size) {
    // 计算起始页面编号（地址除以页面大小）
    long long StartPageNumber = Start / PAGE_SIZE;
    // 计算结束页面编号，减1是为了处理边界情况（包含起始地址+Size-1的页面）
    long long EndPageNumber = (Start + Size - 1) / PAGE_SIZE;

    // 遍历所有相关的页面
    for (long long i = StartPageNumber; i <= EndPageNumber; i++) {
        // 检查页面是否已被任务占用
        if (Pages[i].OccupiedTask != nullptr) {
            return 0;  // 发现被占用的页面，返回不可用
        }
    }

    return 1;  // 所有页面都空闲，返回可用
}

// ===================== Task 类成员函数实现 =====================

// 任务构造函数
// 参数：s - 任务标识符字符串
// 功能：根据随机概率分布初始化任务的内存需求和执行时间
Task::Task(string s) {
    TaskId = s;  // 设置任务标识符

    // 随机决定任务类型（基于内存大小分布）
    int RandType = GetRandomIntThreadSafe() % 100 + 1;  // 生成1-100的随机数

    // 按预设比例分配内存大小
    if (RandType <= 60) {
        // 60%概率：小内存任务 (1-128字节)
        Size = (GetRandomIntThreadSafe() % 128LL) + 1;
    }
    else if (RandType <= 95) {
        // 35%概率：中等内存任务 (固定4KB)
        Size = 4LL * 1024;
    }
    else {
        // 5%概率：大内存任务 (4KB-16MB范围随机)
        Size = (GetRandomIntThreadSafe() % (16LL * 1024 * 1024 - 4 * 1024 + 1)) + 4LL * 1024;
    }

    // 随机生成任务执行时间（1-5秒）
    TotalTime = (GetRandomIntThreadSafe() % 5) + 1;
    RemainingTime = TotalTime;  // 初始化剩余时间为总时间
}

// 任务比较运算符重载（用于优先队列）
// 实现小顶堆排序，剩余时间少的任务优先级高
bool Task::operator<(const Task& other) const {
    return RemainingTime > other.RemainingTime;
}

// ===================== CPU 类成员函数实现 =====================

// CPU初始化函数
// 功能：为CPU创建指定数量的待处理任务
bool CPU::Init() {
    string Console;  // 日志消息缓冲区
    
    // 输出CPU初始化开始日志
    Console = "CPU " + to_string(CPUNumber) + " 初始化开始。";
    os.Print(Console);

    // 初始化任务池，创建TASK_OF_EACH_CPU个任务
    for (int i = 0; i < TASK_OF_EACH_CPU; i++) {
        // 生成任务标识符
        string TaskInfo = "[CPU " + to_string(CPUNumber) + " 的任务 " + to_string(i) + " ]";
        Task task(TaskInfo);  // 创建任务对象
        TaskPool.push_back(task);  // 添加到任务池
    }

    // 输出CPU初始化完成日志
    Console = "CPU " + to_string(CPUNumber) + " 初始化完成。";
    os.Print(Console);

    return 1;  // 返回成功状态
}

// CPU工作函数（核心调度逻辑）
// 功能：执行任务调度、内存分配和中断处理
void CPU::CPUWork() {
    string Console;  // 日志消息缓冲区
    
    // 输出工作开始日志
    Console = "CPU " + to_string(CPUNumber) + " 开始工作。";
    os.Print(Console);

    // 主任务执行循环，直到任务池为空
    while (!TaskPool.empty()) {
        // 选择剩余时间最短的任务（最短作业优先调度策略）
        auto ShortestTask = min_element(TaskPool.begin(), TaskPool.end(),
            [](const Task& a, const Task& b) {
                return a.RemainingTime < b.RemainingTime;
            });

        // 获取最短任务的索引位置
        TaskNumber = (int)distance(TaskPool.begin(), ShortestTask);

        // 输出任务选择信息
        Console = "CPU " + to_string(CPUNumber) +
            " 选择任务：" + TaskPool[TaskNumber].TaskId +
            " （剩余时间：" + to_string(TaskPool[TaskNumber].RemainingTime) + " s）";
        os.Print(Console);

        // 检查任务是否需要内存分配（Start为0表示未分配）
        if (TaskPool[TaskNumber].Start == 0) {
            // 请求操作系统分配内存
            if (!os.New(*this)) {
                // 内存分配失败，放弃该任务
                Console = "CPU " + to_string(CPUNumber) +
                    " 内存分配失败，跳过任务：" + TaskPool[TaskNumber].TaskId;
                os.Print(Console);
                TaskPool.erase(TaskPool.begin() + TaskNumber);  // 从任务池移除
                continue;  // 继续处理下一个任务
            }
            // 内存分配成功，输出详细信息
            Console = "CPU " + to_string(CPUNumber) +
                " 分配内存起始地址：" + to_string(TaskPool[TaskNumber].Start) +
                " (大小:" + to_string(TaskPool[TaskNumber].Size) + " B)";
            os.Print(Console);
        }

        // 任务执行阶段：每秒递减剩余时间并检查中断
        while (TaskPool[TaskNumber].RemainingTime > 0) {
            this_thread::sleep_for(chrono::seconds(1));  // 模拟1秒执行时间
            TaskPool[TaskNumber].RemainingTime--;  // 减少剩余时间

            // 30%概率触发中断（模拟真实系统的中断机制）
            if (GetRandomIntThreadSafe() % 10 < 3) {
                os.Trap(*this);  // 调用操作系统中断处理

                // 如果任务还有剩余时间，保存状态并放回任务池
                if (TaskPool[TaskNumber].RemainingTime > 0) {
                    Console = "CPU " + to_string(CPUNumber) +
                        " 中断保存: " + TaskPool[TaskNumber].TaskId +
                        " (剩余:" + to_string(TaskPool[TaskNumber].RemainingTime) + "s)";
                    os.Print(Console);
                }

                break;  // 退出当前时间片，进行任务切换
            }
        }

        // 检查任务是否完成（剩余时间为0）
        if (TaskPool[TaskNumber].RemainingTime == 0) {
            // 输出任务完成信息
            Console = "CPU " + to_string(CPUNumber) +
                " 完成任务: " + TaskPool[TaskNumber].TaskId;
            os.Print(Console);
            
            os.Free(*this);  // 释放任务占用的内存
            TaskPool.erase(TaskPool.begin() + TaskNumber);  // 从任务池移除已完成任务
        }
    }

    // 输出工作结束日志
    Console = "CPU " + to_string(CPUNumber) + " 工作结束。";
    os.Print(Console);
}

// ===================== OS 类成员函数实现 =====================

// 操作系统初始化函数
// 功能：初始化内存系统和所有CPU单元
bool OS::Init() {
    string Console;  // 日志消息缓冲区

    // 输出OS初始化开始日志
    Console = "OS初始化开始。";
    Print(Console);

    // 初始化内存管理系统
    MEMORY.Init();

    // 随机确定CPU数量（1到CPU_MAX_NUMBER之间）
    int NumberOfCPU = 0;
    NumberOfCPU = GetRandomIntThreadSafe() % CPU_MAX_NUMBER + 1;

    // 输出CPU数量信息
    Console = "随机的CPU数目为：" + to_string(NumberOfCPU);
    Print(Console);

    // 初始化所有CPU单元
    for (int i = 0; i < NumberOfCPU; i++) {
        CPU cpu;  // 创建CPU对象
        cpu.CPUNumber = i;  // 设置CPU编号
        cpu.Init();  // 初始化CPU
        CPUS.push_back(cpu);  // 添加到CPU列表
    }

    // 输出OS初始化完成日志
    Console = "OS初始化完成。";
    Print(Console);

    return 1;  // 返回成功状态
}

// 操作系统启动函数
// 功能：创建并管理所有CPU线程的执行
void OS::Run() {
    // 创建CPU线程容器
    vector<thread>CpuThreads;
    
    // 为每个CPU创建执行线程
    for (auto& cpu : CPUS) {
        CpuThreads.push_back(std::thread(
            [&cpu]() { cpu.CPUWork(); }  // Lambda表达式，调用CPU的工作函数
        ));
    }

    // 等待所有CPU线程执行完成
    for (auto& i : CpuThreads) {
        i.join();  // 阻塞等待线程结束
    }
}

// 中断处理函数
// 参数：cpu - 产生中断的CPU引用
// 功能：记录中断信息并执行相应的中断处理程序
void OS::Trap(CPU& cpu) {
    // 构建中断信息消息
    string msg = "中断：CPU " + to_string(cpu.CPUNumber) + 
                "，任务 " + cpu.TaskPool[cpu.TaskNumber].TaskId + 
                " （剩余：" + to_string(cpu.TaskPool[cpu.TaskNumber].RemainingTime) + " s）。";
    Print(msg);  // 输出中断信息
}

// 内存分配函数（系统调用）
// 参数：cpu - 请求内存的CPU引用
// 返回值：true-分配成功，false-分配失败
// 功能：为指定CPU的当前任务分配对齐的内存空间
bool OS::New(CPU& cpu) {
    // 使用lock_guard自动管理互斥锁，确保内存操作的原子性
    lock_guard<mutex> lock(MEMORY.MemoryLock);

    // 获取当前任务的内存需求大小
    long long Size = cpu.TaskPool[cpu.TaskNumber].Size;
    
    // 检查内存大小是否超过单次分配上限（16MB）
    if (Size > ((long long)16 * 1024 * 1024)) {
        return 0;  // 超过限制，分配失败
    }

    // 计算内存对齐要求：找到不小于Size的最小2的幂
    long long InitStart = 1;
    while (1) {
        if (InitStart >= Size) {
            break;  // 找到合适的对齐值
        }
        else {
            InitStart = InitStart * 2;  // 继续倍增
        }
    }

    // 计算总内存大小（页面数×页面大小）
    long long total_memory_size = (long long)PAGE_SIZE * (long long)MEMORY.Pages.size();
    
    // 按照对齐要求搜索可用内存区域
    for (long long Start = InitStart; Start <= total_memory_size - Size; Start = Start + InitStart) {
        // 检查该内存区域是否可用
        if (MEMORY.CheckMemory(Start, Size)) {
            // 计算涉及的页面范围
            long long StartPageNumber = Start / PAGE_SIZE;
            long long EndPageNumber = (Start + Size - 1) / PAGE_SIZE;

            // 标记所有相关页面为被当前任务占用
            for (long long i = StartPageNumber; i <= EndPageNumber; i++) {
                MEMORY.Pages[i].OccupiedTask = &cpu.TaskPool[cpu.TaskNumber];
            }

            // 设置任务的起始地址
            cpu.TaskPool[cpu.TaskNumber].Start = Start;

            return 1;  // 分配成功
        }
    }

    return 0;  // 内存不足，分配失败
}

// 内存释放函数（系统调用）
// 参数：cpu - 请求释放的CPU引用
// 返回值：true-释放成功
// 功能：释放指定CPU当前任务占用的所有内存页面
bool OS::Free(CPU& cpu) {
    // 使用lock_guard自动管理互斥锁，确保线程安全
    lock_guard<mutex> lock(MEMORY.MemoryLock);

    // 遍历所有内存页面，释放被当前任务占用的页面
    for (auto& p : MEMORY.Pages) {
        if (p.OccupiedTask == &cpu.TaskPool[cpu.TaskNumber]) {
            p.OccupiedTask = nullptr;  // 清除占用标记
        }
    }

    // 重置任务的起始地址
    cpu.TaskPool[cpu.TaskNumber].Start = 0;

    return 1;  // 释放成功
}

// 线程安全的输出函数
// 参数：Text - 要输出的文本内容
// 功能：使用互斥锁确保多线程环境下的输出不会交错
void OS::Print(string& Text) {
    Output.lock();  // 获取输出锁
    cout << Text << endl;  // 输出文本
    Output.unlock();  // 释放输出锁
}

// ===================== 主函数 =====================

// 程序入口点
// 功能：创建操作系统实例并启动运行
int main() {
    os.Init();  // 初始化操作系统
    os.Run();   // 启动操作系统运行
    return 0;   // 程序正常退出
}