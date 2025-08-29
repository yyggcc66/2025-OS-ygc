#pragma once
#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <random>
#include <string>
#include <chrono>
#include <algorithm>

using namespace std;

#define MAX_PAGES 1048576 // 最大页面数
#define MIN_PAGES 16384 // 最小页面数
#define CPU_MAX_NUMBER 8 // 最大CPU数目
#define TASK_OF_EACH_CPU 5 // 每个 CPU 的任务数（测试用）
#define PAGE_SIZE 4096 // 单个页大小 4 KB

int GetRandomIntThreadSafe();

struct Page;
struct Memory;
struct Task;
struct CPU;
struct OS;

// 页面结构体
struct Page
{
	int PageNumber = 0; // 页面编号，从零开始
	const int Size = PAGE_SIZE; // 页面大小
	Task* OccupiedTask = nullptr; // 占用此页的任务
};

// 内存
struct Memory
{
	vector<Page>Pages; // 页面
	mutex MemoryLock; // 总的内存锁，将内存操作约束为串行防止不同步

	// 内存的初始化函数
	bool Init();

	bool CheckMemory(long long Start, long long Size);

};

// 任务结构体
struct Task
{
	string TaskId; // 任务 ID，便于输出
	int TotalTime = 0; // 任务总需要时间（不变）
	int RemainingTime = 0; // 剩余执行时间（动态更新）
	long long Size = 0; // 任务需要的内存大小

	long long Start = 0; // 任务分配的堆栈起始位置，也作为中断时的上下文保存点

	// 构造函数
	Task(string s);

	// 比较函数用于优先队列
	bool operator<(const Task& other) const;

};

// CPU
struct CPU
{
	int CPUNumber = 0; // 编号，从零开始
	vector<Task>TaskPool; // 任务池
	int TaskNumber = 0; // 当前正在执行的任务（从TaskPool）

	// CPU 的初始化函数
	bool Init();

	// 工作函数
	void CPUWork();
};

// 操作系统
struct OS
{
	vector<CPU>CPUS; // CPU
	Memory MEMORY; // 内存
	mutex Output; // 输出函数的锁

	// OS 的初始化函数
	bool Init();

	// 启动函数
	void Run();

	// 中断函数
	void Trap(CPU& cpu);

	// 申请内存，输入为CPU，成功返回 1。
	bool New(CPU& cpu);

	// 释放内存，输入为CPU，成功返回 1。
	bool Free(CPU& cpu);

	// 输出函数
	void Print(string& Text);

};

OS os; // 全局操作系统实例