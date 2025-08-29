#include "L3.h"

// 内存的初始化函数
bool Memory::Init() {

	// 随机一个页面数量
	int SizeOfStack = 0;
	SizeOfStack = GetRandomIntThreadSafe() % (MAX_PAGES - MIN_PAGES + 1) + MIN_PAGES; // 保证结果为64MB~4GB

	string Console;

	Console = "内存初始化开始。";
	os.Print(Console);
	cout << "随机的页面数量为：" << to_string(SizeOfStack) << " 页，约 " << to_string((float)SizeOfStack * PAGE_SIZE / 1048576) << " MB" << endl;
	os.Print(Console);

	for (int i = 0; i < SizeOfStack; i++) {
		Page p;
		p.PageNumber = i;
		Pages.push_back(p);
	}

	Console = "内存初始化完成。";
	os.Print(Console);

	return 1;
}

// 检查某块内存是否已经被占用，
bool Memory::CheckMemory(long long Start, long long Size) {

	long long StartPageNumber = Start / PAGE_SIZE; // 起始页面编号
	long long EndPageNumber = (Start + Size - 1) / PAGE_SIZE; // 结束页面编号，此处减一原因同L1

	for (long long i = StartPageNumber; i <= EndPageNumber; i++) {
		// 若有重叠
		if (Pages[i].OccupiedTask != nullptr) {
			return 0;
		}
	}

	// 没有重叠
	return 1;
}

// 构造函数
Task::Task(string s) {

	TaskId = s;

	// 随机决定分配哪种大小的内存
	int RandType = GetRandomIntThreadSafe() % 100 + 1; // 1-100

	// 按比例分配内存大小
	if (RandType <= 60) {
		// 小内存 (1-128字节)
		Size = (GetRandomIntThreadSafe() % 128LL) + 1;
	}
	else if (RandType <= 95) {
		// 中等内存 (4KB)
		Size = 4LL * 1024;
	}
	else {
		// 大内存 (4KB-16MB)
		Size = (GetRandomIntThreadSafe() % (16LL * 1024 * 1024 - 4 * 1024 + 1)) + 4LL * 1024;
	}

	// 随机时间，1-5秒
	TotalTime = (GetRandomIntThreadSafe() % 5) + 1;
	RemainingTime = TotalTime; // 初始化剩余时间
}

// 比较函数用于优先队列
bool Task::operator<(const Task& other) const {
	return RemainingTime > other.RemainingTime; // 小顶堆
}

// CPU 的初始化函数
bool CPU::Init() {

	string Console;
	// 进入
	Console = "CPU " + to_string(CPUNumber) + " 初始化开始。";
	os.Print(Console);

	// 初始化任务池
	for (int i = 0; i < TASK_OF_EACH_CPU; i++) {
		string TaskInfo = "[CPU " + to_string(CPUNumber) + " 的任务 " + to_string(i) + " ]";
		Task task(TaskInfo);
		TaskPool.push_back(task);
	}

	// 结束
	Console = "CPU " + to_string(CPUNumber) + " 初始化完成。";
	os.Print(Console);

	return 1;
}

// OS 的初始化函数
bool OS::Init() {

	// 输出用
	string Console;

	Console = "OS初始化开始。";
	Print(Console);

	// 初始化内存
	MEMORY.Init();

	// 随机一个CPU数目
	int NumberOfCPU = 0;
	NumberOfCPU = GetRandomIntThreadSafe() % CPU_MAX_NUMBER + 1; // 保证结果为1~8

	Console = "随机的CPU数目为：" + to_string(NumberOfCPU);
	Print(Console);

	// 初始化CPU
	for (int i = 0; i < NumberOfCPU; i++) {
		CPU cpu;
		cpu.CPUNumber = i;
		cpu.Init();
		CPUS.push_back(cpu);
	}

	Console = "OS初始化完成。";
	Print(Console);

	return 1;
}

// 启动函数
void OS::Run() {

	// 开启 CPU 工作流
	vector<thread>CpuThreads;
	for (auto& cpu : CPUS) {
		CpuThreads.push_back(std::thread(
			[&cpu]() { cpu.CPUWork(); } // thread 传入的函数必须显式指定对象实例
		));
	}

	// 等待工作
	for (auto& i : CpuThreads) {
		i.join();
	}

}

// 中断函数
void OS::Trap(CPU& cpu) {

	string msg = "中断：CPU " + to_string(cpu.CPUNumber) + "，任务 " + cpu.TaskPool[cpu.TaskNumber].TaskId + " （剩余：" + to_string(cpu.TaskPool[cpu.TaskNumber].RemainingTime) + " s）。";
	Print(msg);

}

// 申请内存，输入为CPU，成功返回 1。
bool OS::New(CPU& cpu) {

	// 因为有多个 return 路径，手动unlock会很麻烦，使用自动unlock
	lock_guard<mutex> lock(MEMORY.MemoryLock);

	// 超过16MB不行
	long long Size = cpu.TaskPool[cpu.TaskNumber].Size;
	if (Size > ((long long)16 * 1024 * 1024)) {
		return 0;
	}

	// 对于大小为 s 的内存分配请求，返回的内存地址必须对齐到2^i，且 2^i >= s。
	long long InitStart = 1;
	while (1) {

		if (InitStart >= Size) {
			break;
		}
		else {
			// 由于之前的16MB限制，无需担心超过 long long 的范围
			InitStart = InitStart * 2;
		}
	}

	// 搜索可用内存
	for (long long Start = InitStart; Start <= (long long)PAGE_SIZE * (long long)MEMORY.Pages.size() - Size; Start = Start + InitStart) {

		// 找到可用内存
		if (MEMORY.CheckMemory(Start, Size)) {

			long long StartPageNumber = Start / PAGE_SIZE; // 起始页面编号
			long long EndPageNumber = (Start + Size - 1) / PAGE_SIZE; // 结束页面编号，此处减一原因同L1

			for (long long i = StartPageNumber; i <= EndPageNumber; i++) {
				MEMORY.Pages[i].OccupiedTask = &cpu.TaskPool[cpu.TaskNumber];
			}

			cpu.TaskPool[cpu.TaskNumber].Start = Start;

			return 1;
		}
	}

	return 0;
}

// 释放内存，输入为CPU，成功返回 1。
bool OS::Free(CPU& cpu) {

	lock_guard<mutex> lock(MEMORY.MemoryLock);

	// 从页表中移除
	for (auto& p : MEMORY.Pages) {
		if (p.OccupiedTask == &cpu.TaskPool[cpu.TaskNumber]) {
			p.OccupiedTask = nullptr;
		}
	}

	cpu.TaskPool[cpu.TaskNumber].Start = 0;

	return 1;
}

// CPU函数，申请内存并工作
void CPU::CPUWork() {
	string Console;
	// 进入
	Console = "CPU " + to_string(CPUNumber) + " 开始工作。";
	os.Print(Console);

	// 工作
	while (!TaskPool.empty()) {
		// 选择剩余时间最短的任务
		auto ShortestTask = min_element(TaskPool.begin(), TaskPool.end(),
			[](const Task& a, const Task& b) {
				return a.RemainingTime < b.RemainingTime;
			});

		TaskNumber = (int)distance(TaskPool.begin(), ShortestTask);

		Console = "CPU " + to_string(CPUNumber) +
			" 选择任务：" + TaskPool[TaskNumber].TaskId +
			" （剩余时间：" + to_string(TaskPool[TaskNumber].RemainingTime) + " s）";
		os.Print(Console);

		// 申请内存（如果尚未分配）
		if (TaskPool[TaskNumber].Start == 0) {
			if (!os.New(*this)) {
				Console = "CPU " + to_string(CPUNumber) +
					" 内存分配失败，跳过任务：" + TaskPool[TaskNumber].TaskId;
				os.Print(Console);
				TaskPool.erase(TaskPool.begin() + TaskNumber);
				continue;
			}
			Console = "CPU " + to_string(CPUNumber) +
				" 分配内存起始地址：" + to_string(TaskPool[TaskNumber].Start) +
				" (大小:" + to_string(TaskPool[TaskNumber].Size) + " B)";
			os.Print(Console);
		}

		// 执行任务（每秒检查中断）
		while (TaskPool[TaskNumber].RemainingTime > 0) {
			this_thread::sleep_for(chrono::seconds(1));
			TaskPool[TaskNumber].RemainingTime--;

			// 30%概率触发中断
			if (GetRandomIntThreadSafe() % 10 < 3) {
				os.Trap(*this);

				// 如果还有剩余时间，放回任务池
				if (TaskPool[TaskNumber].RemainingTime > 0) {
					Console = "CPU " + to_string(CPUNumber) +
						" 中断保存: " + TaskPool[TaskNumber].TaskId +
						" (剩余:" + to_string(TaskPool[TaskNumber].RemainingTime) + "s)";
					os.Print(Console);
				}

				break; // 退出当前任务执行循环
			}
		}

		// 完成任务处理
		if (TaskPool[TaskNumber].RemainingTime == 0) {
			Console = "CPU " + to_string(CPUNumber) +
				" 完成任务: " + TaskPool[TaskNumber].TaskId;
			os.Print(Console);
			os.Free(*this);
			TaskPool.erase(TaskPool.begin() + TaskNumber);
		}
	}

	// 退出
	Console = "CPU " + to_string(CPUNumber) + " 工作结束。";
	os.Print(Console);
}

// 多线程安全的随机正int数生成器
int GetRandomIntThreadSafe() {
	thread_local mt19937 gen(random_device{}());
	uniform_int_distribution<int> dist(0, 2147483647);
	return dist(gen);
}

// 输出函数
void OS::Print(string& Text) {
	Output.lock();
	cout << Text << endl;
	Output.unlock();
}

// 主函数
int main() {
	os.Init();
	os.Run();
	return 0;
}