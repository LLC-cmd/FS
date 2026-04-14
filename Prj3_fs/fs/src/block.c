#include "block.h"

#include <string.h>

#include "common.h"
#include "log.h"
#include "tcp_utils.h"

superblock sb;

int dcyl;
int dsec;
int port;
tcp_client client;

void zero_block(uint bno) {
    uchar buf[BSIZE];
    memset(buf, 0, BSIZE);
    write_block(bno, buf);
}

uint allocate_block() {
    for (int i = 0; i < sb.size; i += BPB) {
        uchar buf[BSIZE];
        read_block(BBLOCK(i), buf);
        for (int j = 0; j < BPB; j++) {
            int m = 1 << (j % 8);
            if ((buf[j / 8] & m) == 0) {
                buf[j / 8] |= m;
                write_block(BBLOCK(i), buf);
                 if (i + j >= sb.size) {
                    Warn("Out of blocks");
                    return 0;
                }
                zero_block(i + j);
                return i + j;
            }
        }
    }
    Warn("Out of blocks");
    return 0;
}

void free_block(uint bno) {
    uchar bitmap[BSIZE];
    read_block(BBLOCK(bno), bitmap);
    int i = bno % BPB;
    int m = 1 << (i % 8);
    if ((bitmap[i / 8] & m) == 0) {
        Warn("freeing free block");
    }
    bitmap[i / 8] &= ~m;
    write_block(BBLOCK(bno), bitmap);
}

void block_init(int dport){
    port = dport;
    client = client_init("localhost", port);
}

void get_disk_info(int *ncyl, int *nsec) {
    //*ncyl = NCYL, *nsec = NSEC;
    static char msg[BSIZE];
    client_send(client, "I", 2); // ·¢ËÍ 'I' ÃüÁî
    int n = client_recv(client, msg, sizeof(msg));
    msg[n] = 0;
    sscanf(msg, "%d %d", ncyl, nsec);
    dcyl = *ncyl;
    dsec = *nsec;
}

void blockno_to_cylsec(int blockno, int *cyl, int *sec) {
    *cyl = blockno / dsec;
    *sec = blockno % dsec;
}

void read_block(int blockno, uchar *buf) {
    //memcpy(buf, diskfile[blockno], BSIZE);
    int cyl, sec;
    blockno_to_cylsec(blockno, &cyl, &sec);
    char msg[32];
    sprintf(msg, "R %d %d", cyl, sec);
    client_send(client, msg, strlen(msg) + 1);
    int n = client_recv(client, (char*)buf, BSIZE);
    if (n < 0) Error("read block error");
}

void write_block(int blockno, uchar *buf) {
    //memcpy(diskfile[blockno], buf, BSIZE);
    int cyl, sec;
    blockno_to_cylsec(blockno, &cyl, &sec);
    char msg[64 + BSIZE];
    int len = sprintf(msg, "W %d %d %d ", cyl, sec, BSIZE);
    memcpy(msg + len, buf, BSIZE);
    client_send(client, msg, len + BSIZE);
    char ack[8];
    int n = client_recv(client, ack, sizeof(ack));
    if (n < 0) Error("write block error");
}
