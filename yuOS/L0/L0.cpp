/**
 * @file terminal_image_viewer.cpp
 * @brief 在终端中显示彩色图片的工具
 * @details 使用ANSI转义码在终端中渲染图片，支持窗口大小变化时自动重绘
 */

#include <iostream>
#include <string>
#include <algorithm>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>

// 禁用STB库的GIF功能以减小体积
#define STBI_NO_GIF
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

using namespace std;

// ====================== 常量定义 ======================
const string kTerminalResetSequence = "\033[0m";  // 重置终端颜色和样式

// ====================== 终端控制相关 ======================

/**
 * @brief 获取当前终端窗口的尺寸
 * @param[out] columns 存储终端列数(宽度)
 * @param[out] rows 存储终端行数(高度)
 */
void GetTerminalDimensions(int& columns, int& rows) {
    struct winsize window_size;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &window_size);
    columns = window_size.ws_col;
    rows = window_size.ws_row;
}

/**
 * @brief 检查用户是否按下了ESC键
 * @return 如果检测到ESC键按下返回true，否则返回false
 */
bool CheckForEscapeKeyPress() {
    struct termios original_termios, modified_termios;
    
    // 获取当前终端设置
    tcgetattr(STDIN_FILENO, &original_termios);
    modified_termios = original_termios;
    
    // 设置非规范模式，禁用回显
    modified_termios.c_lflag &= ~(ICANON | ECHO);
    modified_termios.c_cc[VMIN] = 0;   // 非阻塞读取
    modified_termios.c_cc[VTIME] = 0;  // 无超时
    
    // 应用临时设置
    tcsetattr(STDIN_FILENO, TCSANOW, &modified_termios);
    
    char key_pressed = 0;
    bool escape_pressed = false;
    
    // 尝试读取一个字符
    if (read(STDIN_FILENO, &key_pressed, 1) > 0) {
        escape_pressed = (key_pressed == 27);  // ESC键的ASCII码是27
    }
    
    // 恢复原始终端设置
    tcsetattr(STDIN_FILENO, TCSANOW, &original_termios);
    
    return escape_pressed;
}

// ====================== 图片渲染相关 ======================

/**
 * @brief 将RGB值格式化为3位数字字符串
 * @param color_value RGB分量值(0-255)
 * @return 3位数字字符串，不足前面补零
 */
string FormatColorComponent(int color_value) {
    string component = to_string(color_value);
    while (component.size() < 3) {
        component = "0" + component;
    }
    return component;
}

/**
 * @brief 在终端中渲染图片
 * @param image_path 要渲染的图片文件路径
 */
void RenderImageInTerminal(const string& image_path) {
    bool should_quit = false;
    
    while (!should_quit) {
        // 获取当前终端尺寸
        int terminal_width, terminal_height;
        GetTerminalDimensions(terminal_width, terminal_height);
        
        // 加载图片数据
        int image_width, image_height, color_channels;
        unsigned char* pixel_data = stbi_load(
            image_path.c_str(), 
            &image_width, 
            &image_height, 
            &color_channels, 
            3  // 强制加载为RGB三通道
        );
        
        if (!pixel_data) {
            cerr << "错误: 无法加载图片文件: " << image_path << endl;
            return;
        }
        
        // 计算保持宽高比的缩放比例
        float scale_factor = min(
            static_cast<float>(terminal_width) / image_width,
            static_cast<float>(terminal_height - 3) / image_height  // 为信息行预留空间
        );
        
        int scaled_width = static_cast<int>(image_width * scale_factor);
        int scaled_height = static_cast<int>(image_height * scale_factor);
        
        // 清屏并显示图片信息
        system("clear");
        cout << "图片信息: 原始尺寸 " << image_width << "x" << image_height 
             << ", 缩放后尺寸 " << scaled_width << "x" << scaled_height << endl;
        cout << "操作提示: 按ESC键退出，调整窗口大小可重新渲染..." << endl;
        
        // 逐行渲染图片
        for (int row = 0; row < scaled_height; ++row) {
            for (int col = 0; col < scaled_width; ++col) {
                // 计算原始图片中的对应位置
                int original_x = min(
                    static_cast<int>(col / scale_factor),
                    image_width - 1  // 防止数组越界
                );
                int original_y = min(
                    static_cast<int>(row / scale_factor),
                    image_height - 1
                );
                
                // 获取RGB像素值
                int pixel_index = (original_y * image_width + original_x) * 3;
                int red = pixel_data[pixel_index];
                int green = pixel_data[pixel_index + 1];
                int blue = pixel_data[pixel_index + 2];
                
                // 生成ANSI背景色转义序列
                string color_sequence = "\033[48;2;" + 
                    FormatColorComponent(red) + ";" +
                    FormatColorComponent(green) + ";" +
                    FormatColorComponent(blue) + "m";
                
                // 输出两个空格作为像素点
                cout << color_sequence << "  " << kTerminalResetSequence;
            }
            cout << endl;  // 换行到下一行像素
        }
        
        // 释放图片内存
        stbi_image_free(pixel_data);
        
        // 等待用户操作或窗口大小变化
        while (true) {
            // 检查窗口大小是否变化
            int current_width, current_height;
            GetTerminalDimensions(current_width, current_height);
            if (current_width != terminal_width || current_height != terminal_height) {
                break;  // 重新渲染以适应新尺寸
            }
            
            // 检查ESC键
            if (CheckForEscapeKeyPress()) {
                should_quit = true;
                break;
            }
            
            // 短暂休眠以减少CPU占用
            usleep(10000);  // 10毫秒
        }
    }
}

// ====================== 主程序 ======================

/**
 * @brief 程序主入口
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 程序退出状态码
 */
int main(int argc, char* argv[]) {
    // 验证命令行参数
    if (argc != 2) {
        cout << "使用方法: " << argv[0] << " <图片路径>" << endl;
        cout << "示例: " << argv[0] << " ~/Pictures/example.jpg" << endl;
        return EXIT_FAILURE;
    }
    
    // 开始渲染图片
    RenderImageInTerminal(argv[1]);
    
    return EXIT_SUCCESS;
}