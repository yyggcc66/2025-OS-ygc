/* 
 * pstree - 以树形结构显示Linux系统中的进程关系
 * 编译: gcc -o pstree pstree.c
 * 运行: ./pstree [-p] [--show-pids] [-V] [--version]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>

#define MAX_PROCESSES 1024        // 最大支持进程数
#define PROC_PATH "/proc"         // proc文件系统路径
#define PROCESS_NAME_MAX 256      // 进程名最大长度
#define FILE_PATH_MAX 256         // 文件路径最大长度

// 进程关系树节点类型
typedef enum {
    SORT_BY_NAME,
    SORT_BY_PID
} sort_type_t;
//_t 后缀是 POSIX 标准定义的命名约定，表示 "type"（类型）

// 子进程链表节点结构
typedef struct child_node {
    int process_index;           // 指向processes数组中子进程的索引
    struct child_node *next;     // 指向下一个子进程节点
} child_node_t;

// 进程信息结构
typedef struct process_info {
    char name[PROCESS_NAME_MAX]; // 进程名
    __pid_t parent_pid;            // 父进程ID
    __pid_t pid;                   // 进程自身ID
    child_node_t *children_head; // 指向子进程链表的头节点
} process_info_t;

// 全局状态
typedef struct {
    int process_count;           // 当前收集到的进程数量
    process_info_t processes[MAX_PROCESSES]; // 进程信息数组
    bool show_pids;              // 是否显示PID的标志
    sort_type_t sort_type;       // 排序方式
} global_state_t;

static global_state_t g_state = {
    .process_count = 0,
    .show_pids = false,
    .sort_type = SORT_BY_NAME
};

/*
 * 读取指定进程ID的统计信息并填充到全局数组中
 */
static bool read_process_info(const char *process_dir_name) {
    char stat_file_path[FILE_PATH_MAX];
    int pid, ppid;
    char process_name[PROCESS_NAME_MAX];
    char state;
    FILE *stat_file;
    
    // 构建stat文件路径，例如 /proc/1234/stat
    snprintf(stat_file_path, sizeof(stat_file_path), 
             "%s/%s/stat", PROC_PATH, process_dir_name);
    
    stat_file = fopen(stat_file_path, "r");
    if (stat_file == NULL) {
        return false; // 文件打开失败（可能进程已结束）
    }

    // 从stat文件中解析出pid, 进程名(在括号内), 状态, ppid
    // stat文件格式: pid (comm) state ppid ...
    if (fscanf(stat_file, "%d (%[^)]) %c %d", &pid, process_name, &state, &ppid) == 4) {
        // 确保不超过数组最大容量
        if (g_state.process_count < MAX_PROCESSES) {
            process_info_t *current_process = &g_state.processes[g_state.process_count];
            
            current_process->pid = pid;
            snprintf(current_process->name, sizeof(current_process->name), "%s", process_name);
            current_process->parent_pid = ppid;
            current_process->children_head = NULL;
            g_state.process_count++;
        }
    }
    
    fclose(stat_file);
    return true;
}

/*
 * 扫描/proc目录，找出所有进程目录并读取其信息
 */
static void scan_all_processes(void) {
    DIR *proc_dir;
    struct dirent *dir_entry;

    proc_dir = opendir(PROC_PATH);
    if (proc_dir == NULL) {
        perror("Failed to open /proc directory");
        exit(EXIT_FAILURE);
    }

    // 遍历/proc目录下的所有条目
    while ((dir_entry = readdir(proc_dir)) != NULL) {
        // 只处理以数字开头的目录名（这些就是进程ID）
        if (isdigit(dir_entry->d_name[0])) {
            read_process_info(dir_entry->d_name);
        }
    }
    
    closedir(proc_dir);
}

/*
 * 根据进程PID查找其在全局数组中的索引位置
 */
static int find_process_index_by_pid(__pid_t target_pid) {
    for (int i = 0; i < g_state.process_count; i++) {
        if (g_state.processes[i].pid == target_pid) {
            return i;
        }
    }
    return -1; // 未找到
}

/*
 * 将子进程节点插入到父进程的子进程链表中，保持排序
 */
static void insert_child_sorted(int parent_index, int child_index) {
    child_node_t *new_child = malloc(sizeof(child_node_t));
    if (new_child == NULL) {
        perror("Failed to allocate memory for child node");
        return;
    }
    
    new_child->process_index = child_index;
    new_child->next = NULL;

    process_info_t *parent_process = &g_state.processes[parent_index];
    
    // 如果父进程还没有子进程，直接作为头节点
    if (parent_process->children_head == NULL) {
        parent_process->children_head = new_child;
        return;
    }

    // 按PID排序插入
    __pid_t child_pid = g_state.processes[child_index].pid;
    __pid_t first_child_pid = g_state.processes[parent_process->children_head->process_index].pid;
    
    // 如果新子进程PID小于当前头节点的PID，插入到头部
    if (child_pid < first_child_pid) {
        new_child->next = parent_process->children_head;
        parent_process->children_head = new_child;
        return;
    }

    // 遍历链表，找到合适的插入位置（保持PID升序）
    child_node_t *current = parent_process->children_head;
    while (current->next != NULL) {
        __pid_t next_child_pid = g_state.processes[current->next->process_index].pid;
        if (child_pid <= next_child_pid) {
            break;
        }
        current = current->next;
    }
    
    // 插入新节点
    new_child->next = current->next;
    current->next = new_child;
}

/*
 * 构建进程树结构，将所有进程组织成树形关系
 */
static void build_process_tree(void) {
    // 查找init进程（PID=1，所有进程的祖先）的索引
    int init_process_index = find_process_index_by_pid(1);
    if (init_process_index == -1) {
        fprintf(stderr, "Error: Init process (PID=1) not found\n");
        return;
    }
    
    // 遍历所有进程，建立父子关系
    for (int i = 0; i < g_state.process_count; i++) {
        if (g_state.processes[i].pid == 1) {
            continue; // 跳过init进程本身
        }
        
        // 查找当前进程的父进程
        int parent_index = find_process_index_by_pid(g_state.processes[i].parent_pid);
        if (parent_index == -1) {
            // 如果找不到父进程（孤儿进程），将其附加到init进程下
            insert_child_sorted(init_process_index, i);
        } else {
            // 找到父进程，将当前进程作为其子进程
            insert_child_sorted(parent_index, i);
        }
    }
}

/*
 * 递归打印进程树
 */
static void print_process_tree(int process_index, int depth_level) {
    const process_info_t *current_process = &g_state.processes[process_index];
    
    // 根据深度打印缩进（每层缩进4个空格）
    for (int i = 0; i < depth_level; i++) {
        printf("    ");
    }
    
    // 打印进程名称
    printf("%s", current_process->name);
    
    // 如果设置了显示PID选项，在括号中打印PID
    if (g_state.show_pids) {
        printf("(%d)", current_process->pid);
    }
    printf("\n");
    
    // 递归打印所有子进程
    child_node_t *current_child = current_process->children_head;
    while (current_child != NULL) {
        print_process_tree(current_child->process_index, depth_level + 1);
        current_child = current_child->next;
    }
}

/*
 * 释放为子进程链表分配的所有内存
 */
static void free_process_tree(void) {
    for (int i = 0; i < g_state.process_count; i++) {
        child_node_t *current = g_state.processes[i].children_head;
        while (current != NULL) {
            child_node_t *next = current->next;
            free(current);
            current = next;
        }
        g_state.processes[i].children_head = NULL;
    }
}

/*
 * 显示使用帮助
 */
static void print_usage(const char *program_name) {
    fprintf(stderr, "Usage: %s [-p] [--show-pids] [-V] [--version]\n", program_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -p, --show-pids    Show PIDs\n");
    fprintf(stderr, "  -V, --version      Show version information\n");
}

/*
 * 主函数
 */
int main(int argc, char *argv[]) {
    // 定义命令行选项
    static const struct option long_options[] = {
        {"show-pids",    no_argument, NULL, 'p'},
        {"numeric-sort", no_argument, NULL, 'n'},
        {"version",      no_argument, NULL, 'V'},
        {NULL, 0, NULL, 0}
    };
    
    int option;
    int option_index = 0;
    
    // 解析命令行参数
    while ((option = getopt_long(argc, argv, "npV", long_options, &option_index)) != -1) {
        switch (option) {
            case 'p': // -p 或 --show-pids
                g_state.show_pids = true;
                break;
                
            case 'V': // -V 或 --version
                printf("pstree version 1.0\n");
                return EXIT_SUCCESS;
                
            case 'n': // -n 或 --numeric-sort
                g_state.sort_type = SORT_BY_PID;
                break;
                
            case '?': // 无效选项
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    // 扫描进程信息
    scan_all_processes();
    
    // 构建进程树
    build_process_tree();
    
    // 查找并从init进程开始打印
    int init_index = find_process_index_by_pid(1);
    if (init_index != -1) {
        print_process_tree(init_index, 0);
    } else {
        fprintf(stderr, "Error: Cannot find init process (PID=1)\n");
    }
    
    // 清理资源
    free_process_tree();
    
    return EXIT_SUCCESS;
}