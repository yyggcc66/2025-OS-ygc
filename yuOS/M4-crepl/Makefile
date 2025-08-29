# 编译器设置
CC = gcc
CFLAGS = -g -Wall -Wextra -std=c11 -pedantic
LDFLAGS = -L./tmp -l:lib.so
INCLUDES = -I.

# 目标可执行文件名
TARGET = crepl

# 源文件
SRCS = crepl.c

# 对象文件
OBJS = $(SRCS:.c=.o)

# 库文件路径
LIB_DIR = ./tmp
LIB_NAME = lib
LIB_SO = $(LIB_DIR)/$(LIB_NAME).so

# 默认目标
all: $(TARGET)

# 链接目标（链接动态库）
$(TARGET): $(OBJS) $(LIB_SO)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

# 编译规则
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# 确保库文件存在（如果需要重新编译库，可以添加相应规则）
$(LIB_SO):
	@echo "Using existing library: $(LIB_SO)"

# 设置运行时库路径（可选）
run: $(TARGET)
	LD_LIBRARY_PATH=./tmp:$$LD_LIBRARY_PATH ./$(TARGET)

# 清理
clean:
	rm -f $(TARGET) $(OBJS)

# 彻底清理（包括库文件，谨慎使用）
distclean: clean
	rm -f *~ *.bak *.tmp

.PHONY: all clean distclean run