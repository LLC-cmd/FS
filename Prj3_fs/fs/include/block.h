#ifndef __BLOCK_H__
#define __BLOCK_H__

#include "common.h"

#define NCYL 64
#define NSEC 32

static uchar diskfile[NCYL * NSEC][BSIZE];

typedef struct {
    uint magic;      // Magic number, used to identify the file system
    uint size;       // Size in blocks
    uint bmapstart;  // Block number of first free map block
    uint inodestart; // Block number of first inode
    uint ninodes;  // How many Inodes
    uint nblocks;  // How many data blocks
    uint first_data_block;  //nmeta
    // Other fields can be added as needed
} superblock;

// sb is defined in block.c
extern superblock sb;

void zero_block(uint bno);
uint allocate_block();
void free_block(uint bno);
void block_init(int dport);
void get_disk_info(int *ncyl, int *nsec);
void read_block(int blockno, uchar *buf);
void write_block(int blockno, uchar *buf);

#endif