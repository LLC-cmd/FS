#include "inode.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "block.h"
#include "log.h"

int global_inode_time = 0; // global time for inode cache used in LRU
inode_cache *cache = NULL;

void inode_cache_init() {
    cache = malloc(inode_cache_size * sizeof(inode_cache));
    if (!cache) {
        Error("inode_cache_init: malloc failed");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < inode_cache_size; i++) {
        cache[i].ip = NULL;
        cache[i].inum = 0;
        cache[i].time = 0;
        cache[i].valid = 0;
    }
}

void inode_cache_free() {
    for (int i = 0; i < inode_cache_size; i++) {
        free(cache[i].ip); // free inode pointers
        cache[i].ip = NULL;
        cache[i].inum = 0;
        cache[i].time = 0;
        cache[i].valid = 0;
    }
    free(cache);
    cache = NULL;
}

inode *iget(uint inum) {
    if (inum < 0 || inum >= sb.ninodes) {
        Warn("iget: inum %d out of range", inum);
        return NULL;
    }
    for (int i = 0; i < inode_cache_size; i++) {
        if (cache[i].valid && cache[i].inum == inum && cache[i].ip) {
            return cache[i].ip; // found in cache
        }
    }
    uint iblock = sb.inodestart + inum / IPB;  //dinode
    uchar buf[BSIZE];
    read_block(iblock, buf);
    dinode *dip = (dinode *)buf + (inum % IPB);
    if (dip->type == 0) {
        Warn("iget: no such inode inum %d", inum);
        return NULL;
    }
    inode *ip = calloc(1, sizeof(inode));   //inode
    ip->inum = inum;
    ip->type = dip->type; 
    ip->mode = dip->mode;
    ip->uid = dip->uid;
    ip->gid = dip->gid;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    ip->blocks = dip->blocks;
    ip->atime = dip->atime;
    ip->mtime = dip->mtime;
    ip->ctime = dip->ctime;
    memcpy(ip->addr, dip->addr, sizeof(dip->addr));
    return ip;
}

void iput(inode *ip) { 
    //free(ip);
    for (int i = 0; i < inode_cache_size; i++) {
        if (cache[i].valid && cache[i].ip == ip) {  // already in cache
            cache[i].time = global_inode_time++;
            return;
        }
    }
    for (int i = 0; i < inode_cache_size; i++) {    // find an empty slot
        if (!cache[i].valid) {
            cache[i].ip = ip;
            cache[i].inum = ip->inum;
            cache[i].time = global_inode_time++;
            cache[i].valid = 1;
            return;
        }
    }
    int replace_index = 0;
    int min_time = cache[0].time;
    for (int i = 0; i < inode_cache_size; i++) {    // replace with LRU
        if (cache[i].time < min_time) {
            min_time = cache[i].time;
            replace_index = i;
        }
    }
    if (cache[replace_index].ip) {
        free(cache[replace_index].ip);
        cache[replace_index].ip = NULL;
        cache[replace_index].valid = 0;
    }
    cache[replace_index].ip = ip;
    cache[replace_index].inum = ip->inum;
    cache[replace_index].time = global_inode_time++;
    cache[replace_index].valid = 1;
}

void iremove(inode *ip) {
    if (!ip) return;
    for (int i = 0; i < inode_cache_size; i++) {
        if (cache[i].valid && cache[i].ip == ip) {
            cache[i].valid = 0; // invalidate cache entry
            free(cache[i].ip); // free inode pointer
            cache[i].ip = NULL;
            return;
        }
    }
    free(ip); // free inode if not in cache
}

inode *ialloc(short type) {
    uchar buf[BSIZE];
    uint iblock;
    for (int i = 0; i < sb.ninodes; i++){
        iblock = sb.inodestart + i / IPB;
        read_block(iblock, buf);
        dinode *dip = (dinode *)buf + i % IPB;
        if (dip->type == 0) {
            memset(dip, 0, sizeof(dinode));
            uint ntime = time(NULL);
            dip->ctime = ntime;
            dip->atime = ntime;
            dip->mtime = ntime;
            dip->type = type;
            write_block(IBLOCK(i), buf);
            inode *ip = calloc(1, sizeof(inode));
            ip->inum = i;
            ip->type = type;
            ip->ctime = ntime;
            ip->atime = ntime;
            ip->mtime = ntime;
            return ip;
        }
    }
    Error("ialloc: no inodes");
    return NULL;
}

void iupdate(inode *ip) {
    uchar buf[BSIZE];
    uint iblock = sb.inodestart + ip->inum / IPB;  //dinode
    read_block(iblock, buf);
    dinode *dip = (dinode *)buf + (ip->inum % IPB);
    dip->type = ip->type; 
    dip->mode = ip->mode;
    dip->uid = ip->uid;
    dip->gid = ip->gid;
    dip->nlink = ip->nlink;
    dip->size = ip->size;
    dip->blocks = ip->blocks;
    dip->atime = ip->atime;
    dip->mtime = ip->mtime;
    dip->ctime = ip->ctime;
    memcpy(dip->addr, ip->addr, sizeof(ip->addr));
    write_block(iblock, buf);
}

int readi(inode *ip, uchar *dst, uint off, uint n) {
    if (ip->size == 0) return 0;    //empty file
    if (off > ip->size || off + n < off) return -1; //offinvalid
    if (off + n > ip->size) n = ip->size - off; //update n

    uint bno;   // bno: phsical block num
    uint len = 0, m;
    uchar buf[BSIZE];
    for (int i = off / BSIZE; i <= (off + n - 1) / BSIZE; i++) {    // i: logic block num
        if (i < NDIRECT) {
            bno = ip->addr[i];
        }
        else if (i < NDIRECT + APB) {
                read_block(ip->addr[NDIRECT], buf);
                uint *t = (uint *)buf;
                bno = t[i - NDIRECT];
            }
            else if (i < MAXFILEB) {
                    read_block(ip->addr[NDIRECT + 1], buf);
                    uint *t1 = (uint *)buf;
                    int j = (i - NDIRECT - APB) / APB;
                    int r = (i - NDIRECT - APB) % APB;
                    uint k = t1[j];
                    read_block(k, buf);
                    uint *t2 = (uint *)buf;
                    bno = t2[r];
                }
        if (bno == 0) {
            Warn("iwrite: invalid block number");
            return -1;
        }
        // read
        read_block(bno, buf);
        uint left = BSIZE;
        uint offset = 0;
        if (i == off / BSIZE) { //start from off
            left = BSIZE - (off % BSIZE);
            offset = off % BSIZE;
        }
        m = min(left, n - len);
        memcpy(dst + len, buf + offset, m);
        len += m;
    }

    ip->atime = time(NULL);

    iupdate(ip);
    return len;
}

int writei(inode *ip, uchar *src, uint off, uint n) {   //overwrite from off
    if (off + n < off) return -1;  // off is invalid
    if (off > ip->size) off = ip->size;
    if (off + n > MAXFILEB * BSIZE) return -1;  // too large

    uchar buf[BSIZE];
    uint bno;   // bno: phsical block num
    uint len = 0, m;
    for (int i = off / BSIZE; i <= (off + n - 1) / BSIZE; i++) {    // i: logic block num
        if (i < NDIRECT) {
            if (!ip->addr[i]) {
                ip->addr[i] = allocate_block();
                if (!ip->addr[i]) return -1;
            }
            bno = ip->addr[i];
        }
        else if (i < NDIRECT + APB) {
                if (!ip->addr[NDIRECT]) {
                    ip->addr[NDIRECT] = allocate_block();
                    if (!ip->addr[NDIRECT]) return -1;
                }
                read_block(ip->addr[NDIRECT], buf);
                uint *t = (uint *)buf;
                if (!t[i - NDIRECT]) {
                    t[i - NDIRECT] = allocate_block();
                    if (!t[i - NDIRECT]) return -1;
                }
                write_block(ip->addr[NDIRECT], buf);
                bno = t[i - NDIRECT];
            }
            else if (i < MAXFILEB) {
                    if (!ip->addr[NDIRECT + 1]) {
                        ip->addr[NDIRECT + 1] = allocate_block();
                        if (!ip->addr[NDIRECT + 1]) return -1;
                    }
                    read_block(ip->addr[NDIRECT + 1], buf);
                    uint *t1 = (uint *)buf;
                    int j = (i - NDIRECT - APB) / APB;
                    int r = (i - NDIRECT - APB) % APB;
                    if (!t1[j]) {
                        t1[j] = allocate_block();
                        if (!t1[j]) return -1;
                    }
                    write_block(ip->addr[NDIRECT + 1], buf);
                    uint k = t1[j];
                    read_block(k, buf);
                    uint *t2 = (uint *)buf;
                    if (!t2[r]) {
                        t2[r] = allocate_block();
                        if (!t2[r]) return -1;
                    }
                    write_block(k, buf);
                    bno = t2[r];
                }
        // write
        read_block(bno, buf);
        uint left = BSIZE;
        uint offset = 0;
        if (i == off / BSIZE) { //start from off
            left = BSIZE - (off % BSIZE);
            offset = off % BSIZE;
        }
        m = min(left, n - len);
        memcpy(buf + offset, src + len, m);
        write_block(bno, buf);
        len += m;
    }

    if (off + n > ip->size) {
        ip->size = off + n;
        ip->blocks = (ip->size -1) / BSIZE + 1;        
    }
    uint ntime = time(NULL);
    ip->atime = ntime;
    ip->mtime = ntime;

    iupdate(ip);
    return n;
}
