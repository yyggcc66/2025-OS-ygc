#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <regex.h>

// 声明外部环境变量指针
extern char **__environ;
// 用于存储子进程（strace）执行参数的数组
char **child_argv;
// 用于存储strace输出文件的路径（实际上是指向管道写端的符号链接）
char strace_output_file_path[64];

// 管道文件描述符，0为读端，1为写端
int pipefd[2];

// 函数声明
void display_results();
void initialize_child_arguments(int argc, char *argv[]);
void run_child_process();
void run_parent_process();
void process_line(char *line);
void update_system_call_info(const char *syscall_name, double time_spent);
int compare_syscall_info(const void *a, const void *b);

// 定义结构体存储系统调用信息（名称和累计时间）
typedef struct SystemCallInfo
{
    char name[64];
    double total_time;
} SystemCallInfo;

// 全局变量：存储所有不同的系统调用信息
SystemCallInfo syscall_info_list[400];
// 当前已记录的系统调用数量
static int syscall_count = 0;
// 所有系统调用的总时间
double total_time_spent = 0.0;

/******************************************************************
 * 主函数
 ******************************************************************/
int main(int argc, char *argv[])
{
    // 刷新标准输出缓冲区
    fflush(stdout);

    // 为子进程参数分配内存 (strace + 选项 + 用户命令 + NULL结尾)
    child_argv = (char **)malloc((argc + 4) * sizeof(char *));
    // 初始化子进程参数数组（最后一个元素设为NULL）
    child_argv[argc + 3] = NULL;
    initialize_child_arguments(argc, argv);

    // 创建管道，用于父子进程通信
    if (pipe(pipefd) < 0)
    {
        perror("创建管道失败");
        exit(EXIT_FAILURE);
    }

    // 创建子进程
    pid_t child_pid = fork();
    if (child_pid == 0)
    {
        // 子进程：执行strace
        run_child_process();
    }
    else if (child_pid > 0)
    {
        // 父进程：读取、分析strace输出并显示结果
        run_parent_process();
    }
    else
    {
        // fork失败
        perror("创建子进程失败");
        exit(EXIT_FAILURE);
    }

    // 主进程不会执行到这里，因为parent()中会等待子进程并退出
    return 0;
}

/******************************************************************
 * 初始化传递给strace子进程的参数数组
 * 格式: /bin/strace -o /proc/<pid>/fd/<pipefd> -T <user_command> <user_args>...
 ******************************************************************/
void initialize_child_arguments(int argc, char *argv[])
{
    child_argv[0] = "/bin/strace"; // strace程序路径
    child_argv[1] = "-o";          // -o 选项，指定输出文件
    // child_argv[2] 将在子进程中动态设置为管道文件描述符的路径
    child_argv[3] = "-T"; // -T 选项，显示系统调用耗时

    // 将用户输入的命令参数复制到child_argv中合适的位置
    for (int i = 1; i < argc; i++)
    {
        child_argv[i + 3] = argv[i];
    }
}

/******************************************************************
 * 子进程执行流程
 * 1. 关闭不需要的管道读端
 * 2. 构造特殊的输出文件路径（指向管道的写端）
 * 3. 使用execve执行strace命令
 ******************************************************************/
void run_child_process()
{
    // 子进程不需要读管道，关闭读端
    close(pipefd[0]);

    // 构造一个特殊路径：/proc/自身PID/fd/管道写端FD
    // strace会将输出写入这个“文件”，也就是写入管道
    // 注意：缓冲区有限，如果输出量大可能会阻塞
    snprintf(strace_output_file_path, sizeof(strace_output_file_path),
             "/proc/%d/fd/%d", getpid(), pipefd[1]);
    child_argv[2] = strace_output_file_path;

    // 执行strace命令，替换当前进程映像
    execve("/bin/strace", child_argv, __environ);

    // 如果execve成功，不会执行到这里
    perror("execve执行失败");
    exit(EXIT_FAILURE);
}

/******************************************************************
 * 检查字符串中是否包含特定字符
 * @param line 要搜索的字符串
 * @param target 要查找的目标字符
 * @return 1找到，0未找到
 ******************************************************************/
int character_exists(const char *line, char target)
{
    for (int idx = 0; line[idx] != '\0'; idx++)
    {
        if (line[idx] == target)
        {
            return 1;
        }
    }
    return 0;
}

/******************************************************************
 * 更新系统调用信息表
 * 如果系统调用已存在，累加时间；否则，添加新条目
 ******************************************************************/
void update_system_call_info(const char *syscall_name, double time_spent)
{
    // 检查是否已记录该系统调用
    for (int i = 0; i < syscall_count; i++)
    {
        if (strcmp(syscall_name, syscall_info_list[i].name) == 0)
        {
            // 找到，累加时间
            syscall_info_list[i].total_time += time_spent;
            return;
        }
    }

    // 未找到，添加新条目
    if (syscall_count < 400)
    { // 防止数组越界
        strncpy(syscall_info_list[syscall_count].name, syscall_name, sizeof(syscall_info_list[syscall_count].name) - 1);
        syscall_info_list[syscall_count].name[sizeof(syscall_info_list[syscall_count].name) - 1] = '\0'; // 确保终止
        syscall_info_list[syscall_count].total_time = time_spent;
        syscall_count++;
    }
}

/******************************************************************
 * 处理从strace输出中读取的一行文本
 * 提取系统调用名称和耗时，并更新全局信息
 ******************************************************************/
void process_line(char *line)
{
    // 简单的过滤：有效的strace输出行通常包含'<'和'>'（用于包裹耗时）
    if (!character_exists(line, '<') || !character_exists(line, '>'))
    {
        return;
    }

    char syscall_name[64] = {0};
    char time_str[64] = {0};
    double time_value = 0.0;

    // 提取系统调用名称：从行首到'('字符之前
    int idx = 0;
    while (line[idx] != '(' && line[idx] != '\0' && idx < sizeof(syscall_name) - 1)
    {
        syscall_name[idx] = line[idx];
        idx++;
    }
    syscall_name[idx] = '\0'; // 确保字符串终止

    // 提取耗时字符串：位于'<'和'>'之间的部分
    // 使用%*[^<]跳过直到第一个'<'之前的所有字符，然后读取<'和'>'之间的内容
    if (sscanf(line, "%*[^<]<%63[^>]>", time_str) == 1)
    {
        // 将耗时字符串转换为double类型
        if (sscanf(time_str, "%lf", &time_value) == 1)
        {
            // 更新总时间和系统调用信息表
            total_time_spent += time_value;
            update_system_call_info(syscall_name, time_value);
        }
    }
}

/******************************************************************
 * 用于qsort的比较函数：按系统调用耗时降序排序
 ******************************************************************/
int compare_syscall_info(const void *a, const void *b)
{
    const SystemCallInfo *info_a = (const SystemCallInfo *)a;
    const SystemCallInfo *info_b = (const SystemCallInfo *)b;

    // 返回值的意义：
    // <0 : a排在b前面
    // >0 : a排在b后面
    // 为了实现降序，用b的时间减去a的时间
    if (info_b->total_time > info_a->total_time)
        return 1;
    if (info_b->total_time < info_a->total_time)
        return -1;
    return 0;
}

/******************************************************************
 * 父进程执行流程
 * 1. 关闭不需要的管道写端。
 * 2. 从管道读端循环读取strace的输出。
 * 3. 按行处理输出，提取信息。
 * 4. 处理完成后，排序并显示结果。
 ******************************************************************/
void run_parent_process()
{
    // 父进程不需要写管道，关闭写端
    close(pipefd[1]);

#define READ_BUFFER_SIZE 4096
    char read_buffer[READ_BUFFER_SIZE];
    char line_buffer[1024];
    ssize_t bytes_read;
    size_t buffer_index = 0;
    size_t line_index = 0;

    // 循环读取管道数据
    while ((bytes_read = read(pipefd[0], read_buffer, READ_BUFFER_SIZE)) > 0)
    {
        buffer_index = 0;
        // 处理本次读取的缓冲区
        while (buffer_index < bytes_read)
        {
            // 将字符从读取缓冲区复制到行缓冲区
            line_buffer[line_index++] = read_buffer[buffer_index++];
            // 如果遇到换行符，说明一行结束
            if (line_buffer[line_index - 1] == '\n')
            {
                line_buffer[line_index] = '\0'; // 确保字符串终止
                process_line(line_buffer);      // 处理这一行
                line_index = 0;                 // 重置行索引，准备读取下一行
                memset(line_buffer, 0, sizeof(line_buffer));
            }
        }
    }

    // 对收集到的系统调用信息按耗时降序排序
    qsort(syscall_info_list, syscall_count, sizeof(SystemCallInfo), compare_syscall_info);

    // 调用显示函数，绘制结果
    display_results();
}

// ------------------------------- 图形显示模块 ------------------------------------

// 定义要在图中显示的不同系统调用的最大个数（前5个，其余归为Other）
#define MAX_SYSCALLS_TO_DISPLAY 5
// 定义终端显示窗口的高度（行数）
#define DISPLAY_WINDOW_HEIGHT 30
// 定义终端显示窗口的宽度（列数）
#define DISPLAY_WINDOW_WIDTH 120

// 定义不同区块的显示格式（颜色代码）
const char *display_formats[MAX_SYSCALLS_TO_DISPLAY] = {
    "\e[42;37m%s\e[0m", // 绿底白字
    "\e[45;37m%s\e[0m", // 紫底白字
    "\e[43;37m%s\e[0m", // 黄底白字
    "\e[44;37m%s\e[0m", // 蓝底白字
    "\e[46;37m%s\e[0m"  // 青底白字
};

// 使用指定格式和索引打印字符串
#define PRINT_FORMATTED(index, string) (fprintf(stderr, display_formats[(index)], (string)))

// 光标的移动宏
#define MOVE_CURSOR_UP (fprintf(stderr, "\e[1A"))
#define MOVE_CURSOR_DOWN (fprintf(stderr, "\e[1B"))
#define MOVE_CURSOR_LEFT (fprintf(stderr, "\e[1D"))
#define MOVE_CURSOR_RIGHT (fprintf(stderr, "\e[1C"))

// 移动光标到指定行列 (1-based, 但通常\e[0;0H是左上角)
#define RESET_CURSOR_POSITION() (fprintf(stderr, "\e[0;0H"))

// 辅助函数：求最小值
int min(int a, int b)
{
    return (a < b) ? a : b;
}

/******************************************************************
 * 在终端上绘制结果条形图
 * 按耗时比例划分区域，显示前MAX_SYSCALLS_TO_DISPLAY个系统调用
 ******************************************************************/
void display_results()
{
    int remaining_width = DISPLAY_WINDOW_WIDTH;
    int remaining_height = DISPLAY_WINDOW_HEIGHT;
    double others_percentage = 1.0; // 初始化为100%，随后逐步减去前几项的比例

    // 遍历前N-1个系统调用（最后一个位置留给Others）
    for (int i = 0; i < min(MAX_SYSCALLS_TO_DISPLAY - 1, syscall_count); i++)
    {
        RESET_CURSOR_POSITION();
        // 移动到当前绘制区域的起始位置（累计偏移）
        for (int down = 0; down < DISPLAY_WINDOW_HEIGHT - remaining_height; down++)
            MOVE_CURSOR_DOWN;
        for (int right = 0; right < DISPLAY_WINDOW_WIDTH - remaining_width; right++)
            MOVE_CURSOR_RIGHT;

        double current_percentage = syscall_info_list[i].total_time / total_time_spent;
        others_percentage -= current_percentage; // 更新Others的比例

        char percentage_label[16];
        snprintf(percentage_label, sizeof(percentage_label), "%d%%", (int)(current_percentage * 100 + 0.5)); // 四舍五入

        if (i % 2 == 0)
        {
            // 水平方向切割：计算当前系统调用应占的宽度
            int block_width = (int)(current_percentage * remaining_width + 0.5);
            if (block_width < 0)
                block_width = 0;
            if (block_width > remaining_width)
                block_width = remaining_width;

            for (int row = 0; row < remaining_height; row++)
            {
                if (row == (remaining_height - 1) / 2)
                {
                    // 在区块中间行打印系统调用名称
                    int name_len = strlen(syscall_info_list[i].name);
                    if (block_width >= name_len)
                    {
                        // 名称可完全显示，居中打印
                        int padding = (block_width - name_len) / 2;
                        for (int s = 0; s < padding; s++)
                            PRINT_FORMATTED(i, " ");
                        PRINT_FORMATTED(i, syscall_info_list[i].name);
                        for (int s = 0; s < block_width - padding - name_len; s++)
                            PRINT_FORMATTED(i, " ");
                    }
                    else
                    {
                        // 宽度不足，打印名称的前block_width个字符（可能截断）
                        // 先将光标左移（超出部分），然后打印（会覆盖）
                        for (int left = 0; left < name_len - block_width; left++)
                            MOVE_CURSOR_LEFT;
                        for (int c = 0; c < block_width; c++)
                        {
                            // 安全地打印每个字符
                            char char_str[2] = {syscall_info_list[i].name[c], '\0'};
                            PRINT_FORMATTED(i, char_str);
                        }
                    }
                    MOVE_CURSOR_DOWN;
                    for (int left = 0; left < block_width; left++)
                        MOVE_CURSOR_LEFT; // 回到行首
                    continue;
                }
                else if (row == (remaining_height - 1) / 2 + 1)
                {
                    // 在名称下一行打印百分比
                    int label_len = strlen(percentage_label);
                    if (block_width >= label_len)
                    {
                        int padding = (block_width - label_len) / 2;
                        for (int s = 0; s < padding; s++)
                            PRINT_FORMATTED(i, " ");
                        PRINT_FORMATTED(i, percentage_label);
                        for (int s = 0; s < block_width - padding - label_len; s++)
                            PRINT_FORMATTED(i, " ");
                    }
                    else
                    {
                        for (int left = 0; left < label_len - block_width; left++)
                            MOVE_CURSOR_LEFT;
                        for (int c = 0; c < block_width; c++)
                        {
                            char char_str[2] = {percentage_label[c], '\0'};
                            PRINT_FORMATTED(i, char_str);
                        }
                    }
                    MOVE_CURSOR_DOWN;
                    for (int left = 0; left < block_width; left++)
                        MOVE_CURSOR_LEFT;
                    continue;
                }
                else
                {
                    // 打印空白的区块行
                    for (int col = 0; col < block_width; col++)
                    {
                        PRINT_FORMATTED(i, " ");
                    }
                    MOVE_CURSOR_DOWN;
                    for (int left = 0; left < block_width; left++)
                        MOVE_CURSOR_LEFT;
                }
            }
            remaining_width -= block_width; // 更新剩余宽度
        }
        else
        {
            // 垂直方向切割：计算当前系统调用应占的高度
            int block_height = (int)(current_percentage * remaining_height + 0.5);
            if (block_height < 0)
                block_height = 0;
            if (block_height > remaining_height)
                block_height = remaining_height;

            for (int row = 0; row < block_height; row++)
            {
                if (row == (block_height - 1) / 2)
                {
                    // 在区块中间行打印系统调用名称（跨整个剩余宽度）
                    int name_len = strlen(syscall_info_list[i].name);
                    if (remaining_width >= name_len)
                    {
                        int padding = (remaining_width - name_len) / 2;
                        for (int s = 0; s < padding; s++)
                            PRINT_FORMATTED(i, " ");
                        PRINT_FORMATTED(i, syscall_info_list[i].name);
                        for (int s = 0; s < remaining_width - padding - name_len; s++)
                            PRINT_FORMATTED(i, " ");
                    }
                    else
                    {
                        for (int left = 0; left < name_len - remaining_width; left++)
                            MOVE_CURSOR_LEFT;
                        for (int c = 0; c < remaining_width; c++)
                        {
                            char char_str[2] = {syscall_info_list[i].name[c], '\0'};
                            PRINT_FORMATTED(i, char_str);
                        }
                    }
                    MOVE_CURSOR_DOWN;
                    for (int left = 0; left < remaining_width; left++)
                        MOVE_CURSOR_LEFT;
                    continue;
                }
                else if (row == (block_height - 1) / 2 + 1)
                {
                    // 打印百分比
                    int label_len = strlen(percentage_label);
                    if (remaining_width >= label_len)
                    {
                        int padding = (remaining_width - label_len) / 2;
                        for (int s = 0; s < padding; s++)
                            PRINT_FORMATTED(i, " ");
                        PRINT_FORMATTED(i, percentage_label);
                        for (int s = 0; s < remaining_width - padding - label_len; s++)
                            PRINT_FORMATTED(i, " ");
                    }
                    else
                    {
                        for (int left = 0; left < label_len - remaining_width; left++)
                            MOVE_CURSOR_LEFT;
                        for (int c = 0; c < remaining_width; c++)
                        {
                            char char_str[2] = {percentage_label[c], '\0'};
                            PRINT_FORMATTED(i, char_str);
                        }
                    }
                    MOVE_CURSOR_DOWN;
                    for (int left = 0; left < remaining_width; left++)
                        MOVE_CURSOR_LEFT;
                    continue;
                }
                else
                {
                    // 打印空白行
                    for (int col = 0; col < remaining_width; col++)
                    {
                        PRINT_FORMATTED(i, " ");
                    }
                    MOVE_CURSOR_DOWN;
                    for (int left = 0; left < remaining_width; left++)
                        MOVE_CURSOR_LEFT;
                }
            }
            remaining_height -= block_height; // 更新剩余高度
        }
    }

    // ----------------------- 绘制Others区块 -----------------------
    RESET_CURSOR_POSITION();
    // 定位到绘制Others区块的起始位置
    for (int down = 0; down < DISPLAY_WINDOW_HEIGHT - remaining_height; down++)
        MOVE_CURSOR_DOWN;
    for (int right = 0; right < DISPLAY_WINDOW_WIDTH - remaining_width; right++)
        MOVE_CURSOR_RIGHT;

    // 确保Others的比例非负
    if (others_percentage < 0)
        others_percentage = 0;
    int others_index = MAX_SYSCALLS_TO_DISPLAY - 1; // Others使用的颜色索引

    for (int row = 0; row < remaining_height; row++)
    {
        if (row == (remaining_height - 1) / 2)
        {
            // 打印"others"文字
            int padding = (remaining_width - 6) / 2; // "others" 长度6
            for (int s = 0; s < padding; s++)
                PRINT_FORMATTED(others_index, " ");
            PRINT_FORMATTED(others_index, "others");
            for (int s = 0; s < remaining_width - padding - 6; s++)
                PRINT_FORMATTED(others_index, " ");
            MOVE_CURSOR_DOWN;
            for (int left = 0; left < remaining_width; left++)
                MOVE_CURSOR_LEFT;
            continue;
        }
        else if (row == (remaining_height - 1) / 2 + 1)
        {
            // 打印Others的百分比
            char others_label[16];
            snprintf(others_label, sizeof(others_label), "%d%%", (int)(others_percentage * 100 + 0.5));
            int label_len = strlen(others_label);
            int padding = (remaining_width - label_len) / 2;
            for (int s = 0; s < padding; s++)
                PRINT_FORMATTED(others_index, " ");
            PRINT_FORMATTED(others_index, others_label);
            for (int s = 0; s < remaining_width - padding - label_len; s++)
                PRINT_FORMATTED(others_index, " ");
            MOVE_CURSOR_DOWN;
            for (int left = 0; left < remaining_width; left++)
                MOVE_CURSOR_LEFT;
            continue;
        }
        else
        {
            // 打印Others的空白行
            for (int col = 0; col < remaining_width; col++)
            {
                PRINT_FORMATTED(others_index, " ");
            }
            MOVE_CURSOR_DOWN;
            for (int left = 0; left < remaining_width; left++)
                MOVE_CURSOR_LEFT;
        }
    }
    // 最后将光标移到窗口最下方，避免提示符覆盖图形
    fprintf(stderr, "\e[%d;0H", DISPLAY_WINDOW_HEIGHT + 1);
}