# 将 CC 改为 g++
CC = g++

# 或者保持 CC = gcc 但添加链接库
LDFLAGS = -lstdc++

# 编译规则
L1: L1.cpp
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)