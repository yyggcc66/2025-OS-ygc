#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <sys/wait.h>
#include <errno.h>

/**
 * 全局常量定义：文件路径与编译选项
 * 作用：统一管理动态生成的 C 源码文件、共享库文件路径，以及 GCC 编译参数
 */
// 动态生成的临时 C 源码文件路径（存放用户输入的函数/表达式包装器）
#define TEMP_C_SOURCE_PATH "./tmp/lib.c"
// 编译生成的临时共享库文件路径（用于动态加载执行）
#define TEMP_SO_LIB_PATH   "./tmp/lib.so"
// GCC 编译选项：生成位置无关代码的共享库
static const char *GCC_COMPILE_ARGS[] = {
    "gcc",          // 编译器路径（默认系统环境变量中的 gcc）
    "-shared",      // 生成共享库（.so 文件）
    "-fPIC",        // 生成位置无关代码（共享库必需）
    TEMP_C_SOURCE_PATH,  // 输入：临时 C 源码文件
    "-o",           // 输出选项标识
    TEMP_SO_LIB_PATH,    // 输出：临时共享库文件
    NULL            // 编译参数结束标记（execvp 要求以 NULL 结尾）
};

/**
 * 表达式包装器模板：将用户输入的表达式封装为可调用函数
 * 例如：用户输入 "1+2"，会生成 "int wrapper_0() { return 1+2; }"
 */
static const char *WRAPPER_TEMPLATE[] = {
    "int ",         // 包装器函数返回值类型（固定为 int，匹配表达式计算结果）
    "wrapper_",     // 包装器函数名前缀（后续拼接数字序号，如 wrapper_0, wrapper_1）
    "() { return ", // 函数体开始：返回用户输入的表达式
    "; }\n",        // 函数体结束：分号收尾 + 换行
    NULL            // 模板结束标记
};

/**
 * 全局变量：表达式包装器计数器
 * 作用：为每个用户输入的表达式分配唯一的包装器函数名（如 wrapper_0, wrapper_1）
 * 避免函数名重复导致编译冲突
 */
static int wrapper_counter = 0;

/**
 * @brief 编译临时共享库：调用 GCC 将临时 C 源码编译为 .so 共享库
 * 实现逻辑：通过 fork 创建子进程，在子进程中调用 execvp 执行 GCC 编译命令
 * 父进程等待子进程编译完成（阻塞），确保编译成功后再进行动态加载
 */
static void compile_temp_lib() {
    // 1. 创建子进程：子进程执行编译，父进程等待
    pid_t child_pid = fork();
    if (child_pid == -1) {
        // fork 失败（如系统资源不足），打印错误并退出
        perror("fork failed: create compile process error");
        exit(EXIT_FAILURE);
    }

    // 2. 子进程逻辑：执行 GCC 编译命令
    if (child_pid == 0) {
        // execvp：从系统环境变量查找 "gcc"，执行编译命令
        // 若执行失败（如 gcc 未安装、tmp 目录不存在），打印错误并退出子进程
        execvp(GCC_COMPILE_ARGS[0], (char *const *)GCC_COMPILE_ARGS);
        perror("execvp failed: gcc compile error");
        exit(EXIT_FAILURE);
    }

    // 3. 父进程逻辑：等待子进程编译完成，忽略编译结果（后续加载时再检查）
    // wait(NULL)：阻塞等待任意子进程退出，不关心子进程退出状态码
    wait(NULL);
}

/**
 * @brief 程序退出清理函数：删除临时生成的 C 源码文件和共享库文件
 * 注册方式：通过 atexit 注册，在程序正常退出（如 Ctrl+C、exit 调用）时自动执行
 * 作用：避免临时文件残留占用磁盘空间
 */
static void cleanup_temp_files() {
    // 删除临时 C 源码文件：忽略删除失败（如文件不存在）
    remove(TEMP_C_SOURCE_PATH);
    // 删除临时共享库文件：忽略删除失败（如文件不存在）
    remove(TEMP_SO_LIB_PATH);
}

/**
 * @brief 处理用户输入的 C 函数声明：将函数追加到临时 C 源码文件
 * @param user_input 用户输入的完整函数声明（如 "int add(int a) { return a+1; }"）
 */
static void handle_function_declaration(const char *user_input) {
    // 以 "追加模式" 打开临时 C 源码文件（若文件不存在则创建）
    FILE *temp_c_file = fopen(TEMP_C_SOURCE_PATH, "a");
    if (temp_c_file == NULL) {
        // 文件打开失败（如 tmp 目录未创建、权限不足），打印错误并返回
        perror("fopen failed: can not open temp C source file for append");
        return;
    }

    // 将用户输入的函数声明追加到文件中，末尾添加换行（确保语法正确）
    fprintf(temp_c_file, "\n%s\n", user_input);
    // 强制刷新缓冲区：确保数据立即写入文件（避免后续编译时读取不到最新内容）
    fflush(temp_c_file);
    // 关闭文件：释放文件描述符，避免资源泄漏
    fclose(temp_c_file);

    // 打印提示信息：告知用户输入已接收（原代码中的 strlen 可能用于调试，保留功能）
    printf("Got %zu chars. Loading...\n", strlen(user_input));
}

/**
 * @brief 处理用户输入的表达式：将表达式封装为包装器函数，编译后执行
 * 实现逻辑：
 * 1. 生成包装器函数（如 wrapper_0() { return 表达式; }），追加到临时 C 源码
 * 2. 编译临时共享库（.so）
 * 3. 动态加载共享库，调用包装器函数执行表达式，输出结果
 * 4. 关闭共享库，更新包装器计数器
 * @param user_input 用户输入的表达式（如 "1+2", "add(3)", "5*6-4"）
 */
static void handle_expression(const char *user_input) {
    // 1. 生成表达式包装器函数，写入临时 C 源码文件
    // 打开临时 C 源码文件（追加模式）
    FILE *temp_c_file = fopen(TEMP_C_SOURCE_PATH, "a");
    if (temp_c_file == NULL) {
        perror("fopen failed: can not open temp C source file for expression");
        return;
    }

    // 处理用户输入的换行符：若输入末尾有 '\n'，去掉（避免表达式中包含换行导致编译错误）
    size_t input_len = strlen(user_input);
    if (input_len > 0 && user_input[input_len - 1] == '\n') {
        input_len--; // 长度减 1，后续截取时忽略最后的 '\n'
    }

    // 拼接包装器函数：使用 WRAPPER_TEMPLATE 模板 + 用户表达式 + 计数器
    // 格式：int wrapper_N() { return [用户表达式]; }
    fprintf(
        temp_c_file,
        "%s%s%d%s%.*s%s",          // 格式化字符串
        WRAPPER_TEMPLATE[0],       // "int "
        WRAPPER_TEMPLATE[1],       // "wrapper_"
        wrapper_counter,           // 包装器序号（如 0, 1, 2）
        WRAPPER_TEMPLATE[2],       // "() { return "
        (int)input_len, user_input,// 用户表达式（截取掉末尾的 '\n'）
        WRAPPER_TEMPLATE[3]        // "; }\n"
    );
    // 强制刷新缓冲区，确保数据写入文件
    fflush(temp_c_file);

    // 2. 编译临时共享库：将包含新包装器的 C 源码编译为 .so
    compile_temp_lib();

    // 3. 动态加载共享库，调用包装器函数执行表达式
    // 打开共享库：RTLD_LAZY 表示延迟绑定（用到函数时才解析符号）
    void *lib_handle = dlopen(TEMP_SO_LIB_PATH, RTLD_LAZY);
    if (lib_handle == NULL) {
        // 加载失败（如编译错误、共享库损坏），打印错误信息（dlerror() 返回具体错误）
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        fclose(temp_c_file); // 关闭文件后返回，避免资源泄漏
        return;
    }

    // 拼接当前包装器函数名（如 "wrapper_0"）
    char wrapper_func_name[64]; // 足够存储 "wrapper_XXXXXX"（计数器最大支持 6 位数字）
    snprintf(
        wrapper_func_name, 
        sizeof(wrapper_func_name), 
        "%s%d", WRAPPER_TEMPLATE[1], wrapper_counter
    );

    // 从共享库中查找包装器函数符号：将 dlsym 返回的 void* 转为函数指针
    // 函数指针类型：int (*)() → 无参数，返回 int
    int (*wrapper_func)() = (int (*)())dlsym(lib_handle, wrapper_func_name);
    if (wrapper_func == NULL) {
        // 符号查找失败（如编译时函数名生成错误），打印错误
        fprintf(stderr, "dlsym failed: %s\n", dlerror());
        dlclose(lib_handle); // 关闭共享库，避免资源泄漏
        fclose(temp_c_file); // 关闭 C 源码文件
        return;
    }

    // 4. 执行包装器函数，获取表达式计算结果并打印
    int expr_result = wrapper_func();
    printf("%d\n", expr_result);

    // 5. 释放资源：关闭共享库和临时 C 源码文件
    dlclose(lib_handle); // 关闭共享库，释放内存
    fclose(temp_c_file); // 关闭 C 源码文件

    // 6. 更新包装器计数器：为下一个表达式分配新的函数名
    wrapper_counter++;
}

/**
 * @brief 主函数：CREPL（C 语言交互式解释器）入口
 * 核心流程：
 * 1. 初始化：注册退出清理函数，确保临时文件被删除
 * 2. 循环：显示提示符 → 读取用户输入 → 区分函数声明/表达式 → 调用对应处理函数
 * 3. 退出：用户输入 EOF（如 Ctrl+D）时，退出循环，触发清理函数
 */
int main(int argc, char *argv[]) {
    // 1. 初始化：注册程序退出时的临时文件清理函数
    // atexit 注册的函数会在 exit() 或 main 返回时自动执行
    atexit(cleanup_temp_files);

    // 2. 初始化临时 C 源码文件：以 "写入模式" 打开（清空文件内容，或创建新文件）
    // 作用：确保每次程序启动时，临时文件是干净的，避免历史内容干扰
    FILE *temp_c_file = fopen(TEMP_C_SOURCE_PATH, "w");
    if (temp_c_file == NULL) {
        // 打开失败（如 tmp 目录未创建），打印错误并退出
        perror("fopen failed: init temp C source file error");
        exit(EXIT_FAILURE);
    }
    fclose(temp_c_file); // 初始化完成后关闭文件（后续按需打开）

    // 3. 交互式循环：读取用户输入并处理
    char user_input[4096]; // 存储用户输入的缓冲区（4KB 足够容纳常规输入）
    while (1) {
        // 显示提示符：告知用户可以输入（类似 bash 的 $，Python 的 >>>）
        printf("crepl> ");
        fflush(stdout); // 强制刷新标准输出（避免提示符因缓冲区未满而不显示）

        // 读取用户输入：fgets 读取一行，存储到 user_input 缓冲区
        // 若读取失败（如 EOF，用户按 Ctrl+D），退出循环
        if (!fgets(user_input, sizeof(user_input), stdin)) {
            printf("\n"); // 输入 EOF 时换行，避免终端提示符错位
            break;
        }

        // 区分用户输入类型：函数声明 vs 表达式
        // 判断依据：若输入以 "int" 开头（且长度 >3，避免空输入误判），视为函数声明
        if (strlen(user_input) > 3 && strncmp(user_input, "int", 3) == 0) {
            handle_function_declaration(user_input);
        } else {
            // 否则视为表达式，调用表达式处理函数
            handle_expression(user_input);
        }
    }

    // 4. 正常退出：main 返回后，atexit 注册的 cleanup_temp_files 会自动执行
    return EXIT_SUCCESS;
}