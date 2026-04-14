#ifndef __INODE_H__
#define __INODE_H__

#include "common.h"

#define NDIRECT 20  // Direct blocks, you can change this value
#define APB (BSIZE / sizeof(uint))
#define MAXFILEB (NDIRECT + APB + APB * APB)

enum {
    T_DIR = 1,   // Directory
    T_FILE = 2,  // File
};

// You should add more fields
// the size of a dinode must divide BSIZE
typedef struct {
    ushort type: 2;                // 文件类型（0=空,1=文件,2=目录等）
    ushort mode: 4;                // 权限（如0755）
    ushort uid: 10;                // 共2B: 用户ID
    ushort gid;               // 2B: 组ID
    uint nlink;               // 4B: 硬链接数
    uint size;                // 4B: 文件大小（字节）
    uint blocks;              // 4B: 已使用的块数
    uint atime;               // 4B: 最后访问时间
    uint mtime;               // 4B: 最后修改时间
    uint ctime;               // 4B: 创建时间

    uint addr[NDIRECT + 2]; // 80B: 20个直接块指针（20 * 4B）     // 4B: 一级间接指针     // 4B: 二级间接指针
    uint reserved[3];         // 12B: 保留字段（对齐用）
} dinode;

// inode in memory
// more useful fields can be added, e.g. reference count
typedef struct {
    uint inum;
    ushort type: 2; 
    ushort mode: 4;
    ushort uid: 10;
    ushort gid;
    uint nlink;
    uint size;
    uint blocks;
    uint atime;
    uint mtime;
    uint ctime;

    uint addr[NDIRECT + 2];
    uint reserved[3];
} inode;

// You can change the size of MAXNAME
#define MAXNAME 12

typedef struct  {  // 16 bytes: used for write in directory
    uint inum;          // 4B
    char name[MAXNAME]; // 12B
} direct;

#define inode_cache_size 10

typedef struct {
    inode *ip;
    uint inum;
    int time;
    int valid;  // 1: valid, 0: invalid
} inode_cache;

// Get an inode by number (returns allocated inode or NULL)
// Don't forget to use iput()
inode *iget(uint inum);

// Free an inode (or decrement reference count)
void iput(inode *ip);

// Remove an inode from cache (used for rm)
void iremove(inode *ip);
// Allocate a new inode of specified type (returns allocated inode or NULL)
// Don't forget to use iput()
inode *ialloc(short type);

// Update disk inode with memory inode contents
void iupdate(inode *ip);

// Read from an inode (returns bytes read or -1 on error)
int readi(inode *ip, uchar *dst, uint off, uint n);

// Write to an inode (returns bytes written or -1 on error)
int writei(inode *ip, uchar *src, uint off, uint n);

void inode_cache_init();
void inode_cache_free();

#endif
