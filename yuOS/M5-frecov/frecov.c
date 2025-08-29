#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>




typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

// FAT32 BPB (BIOS Parameter Block)
struct fat32hdr {
    u8  BS_jmpBoot[3];        // 跳转指令 (EB 58 90 或类似)
    u8  BS_OEMName[8];        // 格式化该卷的OEM名称（字符串）
    u16 BPB_BytsPerSec;       // 每扇区字节数（通常为512）
    u8  BPB_SecPerClus;       // 每簇扇区数（1,2,4,8,16,32,64,128）
    u16 BPB_RsvdSecCnt;       // 保留扇区数（包括引导扇区）
    u8  BPB_NumFATs;          // FAT表的数量（通常为2）
    u16 BPB_RootEntCnt;       // FAT32中此值必须为0
    u16 BPB_TotSec16;         // 总扇区数（16位，若为0则使用BPB_TotSec32）
    u8  BPB_Media;            // 存储介质类型（0xF8=硬盘）
    u16 BPB_FATSz16;          // FAT32中此值必须为0
    u16 BPB_SecPerTrk;        // 每磁道扇区数（磁盘几何参数）
    u16 BPB_NumHeads;         // 磁头数（磁盘几何参数）
    u32 BPB_HiddSec;          // 隐藏扇区数（分区前的扇区数）
    u32 BPB_TotSec32;         // 总扇区数（32位）
    u32 BPB_FATSz32;          // 单个FAT表占用的扇区数（FAT32关键字段）
    u16 BPB_ExtFlags;         // 扩展标志（位7-0：活动FAT索引，位15：镜像禁用）
    u16 BPB_FSVer;            // 文件系统版本（高字节=主版本，低字节=次版本）
    u32 BPB_RootClus;         // 根目录起始簇号（FAT32关键字段）
    u16 BPB_FSInfo;           // FSInfo结构在保留区的扇区号
    u16 BPB_BkBootSec;        // 引导记录备份位置扇区号
    u8  BPB_Reserved[12];     // 保留字段（用于扩展）
    u8  BS_DrvNum;            // 驱动器号（INT 13h使用）
    u8  BS_Reserved1;         // 保留（WindowsNT使用）
    u8  BS_BootSig;           // 扩展引导签名（0x29表示后三个字段存在）
    u32 BS_VolID;             // 卷序列号
    u8  BS_VolLab[11];        // 卷标（字符串）
    u8  BS_FilSysType[8];     // 文件系统类型（"FAT32"字符串）
    u8  __padding_1[420];     // 引导代码和填充字节
    u16 Signature_word;       // 引导扇区结束标志（0xAA55）
} __attribute__((packed));

// FAT32 目录项结构（32字节）
struct fat32dent {
    u8  DIR_Name[11];         // 8.3格式文件名（空格填充）
    u8  DIR_Attr;             // 文件属性（位掩码）
    u8  DIR_NTRes;            // NT保留字节
    u8  DIR_CrtTimeTenth;     // 创建时间的10毫秒单位（0-199）
    u16 DIR_CrtTime;          // 创建时间（小时5位/分6位/秒5位）
    u16 DIR_CrtDate;          // 创建日期（年7位/月4位/日5位）
    u16 DIR_LastAccDate;      // 最后访问日期
    u16 DIR_FstClusHI;        // 起始簇号的高16位（FAT32关键字段）
    u16 DIR_WrtTime;          // 最后写入时间
    u16 DIR_WrtDate;          // 最后写入日期
    u16 DIR_FstClusLO;        // 起始簇号的低16位（FAT32关键字段）
    u32 DIR_FileSize;         // 文件大小字节数（目录时为0）
} __attribute__((packed));

#define CLUS_INVALID   0xffffff7

#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN    0x02
#define ATTR_SYSTEM    0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE   0x20 

static bool debug_enabled= false;

#define DEBUG_PRINT(...) do { if (debug_enabled) printf(__VA_ARGS__); } while(0)

#define BMP_SIGNATURE 0x4D42 // 'BM'

#define TEMP_FILE_TEMPLATE "/tmp/fsrecov_XXXXXX"

#define ATTR_LONG_NAME (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)

#define LAST_LONG_ENTRY 0x40

struct fat32lfn {
    u8  LDIR_Ord;
    u16 LDIR_Name1[5];
    u8  LDIR_Attr;
    u8  LDIR_Type;
    u8  LDIR_Chksum;
    u16 LDIR_Name2[6];
    u16 LDIR_FstClusLO;
    u16 LDIR_Name3[2];
} __attribute__((packed));

struct output_file {
    char* name;
    u8* start;
    u32 size;
};

struct fat32hdr* hdr;
u8* disk_base;
u8* disk_end;
int first_data_sector;
int total_clusters;
int entry_size = sizeof(struct fat32dent);

void* mmap_disk(const char* fname)
{
    int fd = open(fname, O_RDWR);

    if (fd < 0) {
        perror("map disk");
        exit(1);
    }

    off_t size = lseek(fd, 0, SEEK_END);
    if (size < 0) {
        perror("map disk");
        exit(1);
    }

    struct fat32hdr* hdr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (hdr == MAP_FAILED) {
        perror("map disk");
        exit(1);
    }

    close(fd);

    assert(hdr->Signature_word == 0xaa55);
    assert(hdr->BPB_TotSec32 * hdr->BPB_BytsPerSec == size);

    return hdr;
}

bool is_dirent_basic(struct fat32dent* dent)
{   
    // 根据FAT32规范，第一个字节为0x00表示当前目录项及之后的目录项均为空
    if (dent->DIR_Name[0] == 0x00) {
        return false;
    }
    // 如果需要更严格，可以加上8.3文件名的合法字符检查

    // Attr最高2位和NTRes都是保留位，应该为0
    if ((dent->DIR_Attr & 0b11000000) != 0) {
        return false;
    }
    if (dent->DIR_NTRes != 0) {
        return false;
    }
    // dot、dotdot目录和已删除目录项，提前判断，因为簇号可能为0
    if (dent->DIR_Name[0] == '.' || dent->DIR_Name[0] == 0xE5) {
        return true;
    }
    // 簇号
    int clus_num = dent->DIR_FstClusHI << 16 | dent->DIR_FstClusLO;
    if (clus_num < 2 || clus_num > total_clusters + 1) {
        return false;
    }
    // 认为文件大小不超过64MB（仅对本实验有效）
    if (dent->DIR_FileSize > 64 * 1024 * 1024) {
        return false;
    }
    return true;
}

bool is_dirent_long(struct fat32lfn* lfn)
{   
    // 长文件名最多255字符，而每个条目可存13字符，所以条目序号为1-20
    int ord = lfn->LDIR_Ord & ~LAST_LONG_ENTRY;
    if (ord == 0 || ord > 20) {
        return false;
    }
    if (lfn->LDIR_Attr != ATTR_LONG_NAME) {
        return false;
    }
    if (lfn->LDIR_Type != 0) {
        return false;
    }
    if (lfn->LDIR_FstClusLO != 0) {
        return false;
    }
    return true;
}

bool is_dirent_cluster_possibly(u8* cluster_start)
{   
    // 如果簇的前32字节是目录项，则认为该簇包含目录项
    return is_dirent_basic((struct fat32dent*)cluster_start)
        || is_dirent_long((struct fat32lfn*)cluster_start);
}

u8* first_byte_ptr_of_cluster(int clus_num)
{   // FAT32 spec
    int first_sector_of_cluster = first_data_sector + (clus_num - 2) * hdr->BPB_SecPerClus;
    u8* cluster_start = disk_base + first_sector_of_cluster * hdr->BPB_BytsPerSec;
    return cluster_start;
}

u8 calc_checksum(const u8* pFcbName)
{   // FAT32 spec
    u8 sum = 0;
    for (int i = 11; i != 0; i--) {
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + *pFcbName++;
    }
    return sum;
}

void extract_name_from_lfn(struct fat32lfn* lfn, char* name)
{   
    for (int i = 0; i < 5; i++) {
        name[i] = lfn->LDIR_Name1[i];
    }
    for (int i = 0; i < 6; i++) {
        name[i + 5] = lfn->LDIR_Name2[i];
    }
    for (int i = 0; i < 2; i++) {
        name[i + 11] = lfn->LDIR_Name3[i];
    }
    name[13] = '\0';
}

void outprint(struct output_file file)
{
    // 写入临时文件
    char file_path[] = TEMP_FILE_TEMPLATE;
    int fd = mkstemp(file_path);
    assert(fd >= 0);    
    ssize_t bytes_written = write(fd, file.start, file.size);
    close(fd);

    // 计算sha1
    char cmd[256];
    snprintf(cmd, 256, "sha1sum %s", file_path);
    FILE* fp = popen(cmd, "r");
    char sha1[64];
    fscanf(fp, "%s", sha1);
    pclose(fp);

    printf("%s  %s\n", sha1, file.name);

    unlink(file_path);
}

void handle(const u8* long_name_entries, int lfn_count, const struct fat32dent* basic_entry)
{
    // 跳过已删除、目录或无效的条目
    if (basic_entry->DIR_Name[0] == 0xE5 || (basic_entry->DIR_Attr & ATTR_DIRECTORY)) {
        return;
    }
    
    // 检查簇号是否有效
    u32 clus_num = (u32)basic_entry->DIR_FstClusHI << 16 | basic_entry->DIR_FstClusLO;
    if (clus_num < 2 || clus_num > total_clusters + 1) {
        return;
    }
    
    u8* file_start_ptr = first_byte_ptr_of_cluster(clus_num);

    // 检查文件起始处是否为BMP签名
    if (file_start_ptr[0] != 'B' || file_start_ptr[1] != 'M') {
        return;
    }
    
    // 解析文件名
    char file_name[256] = {0};
    if (lfn_count > 0) {
        // 从长文件名条目中反向解析
        for (int i = lfn_count - 1; i >= 0; i--) {
            struct fat32lfn* lfn = (struct fat32lfn*)(long_name_entries + i * entry_size);
            char part_name[14] = {0};
            extract_name_from_lfn(lfn, part_name);
            strncat(file_name, part_name, sizeof(file_name) - strlen(file_name) - 1);
        }
    } else {
        // 如果没有长文件名，则使用短文件名
        strncpy(file_name, (char*)basic_entry->DIR_Name, 8);
        char* ext = (char*)basic_entry->DIR_Name + 8;
        if (ext[0] != ' ') {
            strncat(file_name, ".", 1);
            strncat(file_name, ext, 3);
        }
    }

    // 获取文件大小
    u32 file_size = basic_entry->DIR_FileSize;
    if (file_size == 0 || file_start_ptr + file_size > disk_end) {
        return;
    }

    struct output_file file = {
        .name = file_name,
        .start = file_start_ptr,
        .size = file_size
    };
    outprint(file);
}

void search_cluster(u8* cluster_start, int clus_num)
{
    u8* p = cluster_start;
    u8* long_name_entries = NULL;
    int lfn_count = 0;

    // 遍历簇中的所有目录项
    while (p < cluster_start + hdr->BPB_BytsPerSec * hdr->BPB_SecPerClus) {
        struct fat32lfn* lfn = (struct fat32lfn*)p;
        struct fat32dent* dent = (struct fat32dent*)p;

        if (dent->DIR_Name[0] == 0x00) break;

        if (is_dirent_long(lfn)) {
            // 是长文件名条目，累积起来
            if (!long_name_entries) {
                // 如果是第一个长文件名条目，分配内存
                long_name_entries = malloc(20 * entry_size);
                if (!long_name_entries) {
                    perror("malloc");
                    break;
                }
            }
            memcpy(long_name_entries + lfn_count * entry_size, p, entry_size);
            lfn_count++;
        } else if (is_dirent_basic(dent)) {
            // 是短文件名条目，结合之前累积的长文件名条目进行处理
            // 首先校验长文件名的校验和
            if (lfn_count > 0 && ((struct fat32lfn*)long_name_entries)->LDIR_Chksum != calc_checksum(dent->DIR_Name)) {
                // 校验和不匹配，说明长短文件名不属于同一个文件，丢弃长文件名
                free(long_name_entries);
                long_name_entries = NULL;
                lfn_count = 0;
            }
            // 调用handle处理文件
            handle(long_name_entries, lfn_count, dent);
            
            // 处理完毕，重置长文件名条目缓存
            if (long_name_entries) {
                free(long_name_entries);
                long_name_entries = NULL;
            }
            lfn_count = 0;
        } else {
            // 其他情况，跳过
            if (long_name_entries) {
                free(long_name_entries);
                long_name_entries = NULL;
            }
            lfn_count = 0;
        }
        p += entry_size;
    }

    // 释放可能剩余的内存
    if (long_name_entries) {
        free(long_name_entries);
    }
}

void full_scan()
{
    for (int clus_num = 2; clus_num < total_clusters + 2; clus_num++) {
        u8* cluster_start = first_byte_ptr_of_cluster(clus_num);
        if (is_dirent_cluster_possibly(cluster_start)) {
            search_cluster(cluster_start, clus_num);
        }
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2) exit(1);

    setbuf(stdout, NULL);

    assert(sizeof(struct fat32hdr) == 512);
    assert(sizeof(struct fat32dent) == 32);

    disk_base = mmap_disk(argv[1]);
    hdr = (struct fat32hdr*)disk_base;

    disk_end = disk_base + hdr->BPB_TotSec32 * hdr->BPB_BytsPerSec - 1;
    first_data_sector = hdr->BPB_RsvdSecCnt + hdr->BPB_NumFATs * hdr->BPB_FATSz32;
    total_clusters = (hdr->BPB_TotSec32 - first_data_sector) / hdr->BPB_SecPerClus;

    full_scan();

    munmap(hdr, hdr->BPB_TotSec32 * hdr->BPB_BytsPerSec);

    return 0;
}