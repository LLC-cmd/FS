#include "fs.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "block.h"
#include "log.h"

uint pwd = 0;;
uint users_inum = 1;
uint uid = 0;

// Check if the inode is valid
int checkPerm(inode *ip, short perm) {
    if (ip->uid == uid) return 1; // 属主有全部权限
    if ((ip->mode & perm) == perm) return 1; // 其他用户权限
    return 0;
}

// Create a inode (file or directory) type,name,parent inum,uid,mode
int icreate(short type, char *name, uint pinum, ushort uid, ushort perm) {
    inode *ip = ialloc(type);
    //printf("allocate successfully\n");
    //CheckIP(1);
    ip->mode = perm;
    ip->uid = uid;
    ip->nlink = 1;
    ip->size = 0;
    ip->blocks = 0;
    uint inum = ip->inum;
    if (type == T_DIR) {    // directory
        direct des[2];
        des[0].inum = inum;
        strcpy(des[0].name, ".");   // itself
        des[1].inum = pinum;
        strcpy(des[1].name, "..");  // parent directory
        writei(ip, (uchar *)&des, ip->size, sizeof(des));   // include iupdate()
    } else {
        iupdate(ip);
    }
    //prtinode(ip);
    iput(ip);
    
    //printf("initial successfully\n");
    if (pinum != inum) {  // root directry: inum = 0 = pinum
        ip = iget(pinum);   // pinum must be a directory
        //CheckIP(1);
        direct de;
        de.inum = inum;
        strcpy(de.name, name);
        if (writei(ip, (uchar *)&de, ip->size, sizeof(de)) < 0) {
            return E_ERROR;
        } // add it to the parent directory (append)
        iput(ip);
    }
    //printf("ready to return\n");
    return E_SUCCESS;
}

// find in pwd: NINODES for not found, return inum of the file
uint findinum(char *name) {
    inode *ip = iget(pwd);  // find current directory
    //CheckIP(NINODES);

    uchar *buf = malloc(ip->size);
    readi(ip, buf, 0, ip->size);
    direct *de = (direct *)buf;

    int result = NINODES;   
    int nfile = ip->size / sizeof(direct);
    for (int i = 0; i < nfile; i++) {
        if (de[i].inum == NINODES) continue;  // deleted
        if (strcmp(de[i].name, name) == 0) {    // found
            result = de[i].inum;
            break;
        }
    }
    free(buf);
    iput(ip);
    return result;
}

// free all data blocks of an inode, but not the inode itself
void itrunc(inode *ip) {
    uchar buf[BSIZE];
    int apb = APB;

    for (int i = 0; i < NDIRECT; i++)
        if (ip->addr[i]) {
            free_block(ip->addr[i]);
            ip->addr[i] = 0;
        }

    if (ip->addr[NDIRECT]) {
        read_block(ip->addr[NDIRECT], buf);
        uint *addrs = (uint *)buf;
        for (int i = 0; i < apb; i++) {
            if (addrs[i]) free_block(addrs[i]);
        }
        free_block(ip->addr[NDIRECT]);
        ip->addr[NDIRECT] = 0;
    }

    if (ip->addr[NDIRECT + 1]) {
        read_block(ip->addr[NDIRECT + 1], buf);
        uint *addrs = (uint *)buf;
        uchar buf2[BSIZE];
        for (int i = 0; i < apb; i++) {
            if (addrs[i]) {
                read_block(addrs[i], buf2);
                uint *addrs2 = (uint *)buf2;
                for (int j = 0; j < apb; j++) {
                    if (addrs2[j]) free_block(addrs2[j]); 
                }
                free_block(addrs[i]);
            }
        }
        free_block(ip->addr[NDIRECT + 1]);
        ip->addr[NDIRECT + 1] = 0;
    }
    //ip->type = 0;
    ip->size = 0;
    ip->blocks = 0;
    iupdate(ip);
}

// delete inode from pwd
int delinum(uint inum) {
    inode *ip = iget(pwd);  //  ip is the directory
    //CheckIP(0);

    uchar *buf = malloc(ip->size);
    readi(ip, buf, 0, ip->size);
    direct *de = (direct *)buf;

    int nfile = ip->size / sizeof(direct);
    int deleted = 1;
    for (int i = 0; i < nfile; i++) {
        if (de[i].inum == NINODES) {
            deleted++;
            continue;  // deleted
        }
        if (de[i].inum == inum) {   // delete taget
            de[i].inum = NINODES;
            writei(ip, (uchar *)&de[i], i * sizeof(direct), sizeof(direct));
        }
    }

    if (deleted > nfile / 2) {  // rewrite condition
        int newn = nfile - deleted; // left inodes 
        int newsiz = newn * sizeof(direct); // needed size
        uchar *newbuf = malloc(newsiz);
        direct *newde = (direct *)newbuf;
        int j = 0;
        for (int i = 0; i < nfile; i++) {
            if (de[i].inum == NINODES) continue;  // deleted
            memcpy(&newde[j++], &de[i], sizeof(direct));
        }
        assert(j == newn);
        ip->size = newsiz;
        writei(ip, newbuf, 0, newsiz);
        free(newbuf);
        //itest(ip);  // try to shrink
    }

    free(buf);
    iput(ip);
    return 0;
}

void sbinit() {
    uchar buf[BSIZE];
    read_block(0, buf);
    memcpy(&sb, buf, sizeof(sb));
}

int cmd_f(int ncyl, int nsec) {
    // calculate filesystem info
    uint fsize = ncyl * nsec;
    uint nbitmap = (fsize / BPB) + 1;
    uint ninodes = NINODES;
    uint inodetable = (ninodes * ISIZE) / BSIZE;
    uint nmeta = 1 + nbitmap + inodetable;
    // write for sb
    sb.magic = MAGIC;
    sb.size = fsize;
    sb.bmapstart = 1;
    sb.inodestart = 1 + nbitmap;
    sb.ninodes = ninodes;
    sb.nblocks = fsize - nmeta;
    sb.first_data_block = nmeta;
    // disk[0] init
    uchar buf[BSIZE];
    memset(buf, 0, BSIZE);
    memcpy(buf, &sb, sizeof(sb));
    write_block(0, buf);
    // bitmap init
    memset(buf, 0, BSIZE);
    for (int i = 0; i < sb.size; i += BPB) {
        write_block(BBLOCK(i), buf);
    }
    for (int i = 0; i < sb.ninodes; i += IPB) {
        write_block(IBLOCK(i), buf);
    }
    // mark meta blocks as in use
    for (int i = 0; i < nmeta; i += BPB) {
        memset(buf, 0, BSIZE);
        for (int j = 0; j < BPB; j++) {
            if (i + j < nmeta) {    // limit: i + j < nmeta
                buf[j / 8] |= 1 << (j % 8);
            }  
        }
        write_block(BBLOCK(i), buf);
    }
    inode_cache_free();  // free inode cache
    inode_cache_init();  // initialize inode cache
    if(uid != 1) uid = 0;
    // create root directory
    if (icreate(T_DIR, "home", 0, 0, 0b1111)) {
        Error("Failed to create /home directory");
        return E_ERROR;
    }
    // create users file
    if (icreate(T_FILE, "users", 0, 0, 0b1111)) {
        Error("Failed to create /users file");
        return E_ERROR;
    }
    if(uid != 1) uid = 0;
    else {
        cmd_login(1);
    }
    return E_SUCCESS;
}

int cmd_mk(char *name, short mode) {
    //CheckFmt();
    int len = strlen(name), valid = 1;
    if (len >= MAXNAME) valid = 0;
    if (name[0] == '.') valid = 0;
    if (strcmp(name, "/") == 0) valid = 0;
    if (!valid) {
        Error("Invalid name!");
        return E_ERROR;
    }

    if (findinum(name) != NINODES) {
        Error("Name already exists!");
        return E_ERROR;
    }
    //CheckPerm(pwd, R | W);
    //printf("ready to create!");
    if (icreate(T_FILE, name, pwd, uid, mode)) return E_ERROR;
    return E_SUCCESS;
}

int cmd_mkdir(char *name, short mode) {
    //CheckFmt();
    int len = strlen(name), valid = 1;
    if (len >= MAXNAME) valid = 0;
    if (name[0] == '.') valid = 0;
    if (strcmp(name, "/") == 0) valid = 0;
    if (!valid) {
        Error("Invalid name!");
        return E_ERROR;
    }

    if (findinum(name) != NINODES) {
        Error("Name already exists!");
        return E_ERROR;
    }
    //CheckPerm(pwd, R | W);

    if (icreate(T_DIR, name, pwd, uid, mode)) return E_ERROR;
    return E_SUCCESS;
}

int cmd_rm(char *name) {
    //CheckFmt();
    uint inum = findinum(name);
    if (inum == NINODES) {
        Error("Not found in current directory!");
        return E_ERROR;
    }
    //CheckPerm(inum, W);
    //CheckPerm(pwd, R | W);
    inode *ip = iget(inum);
    //CheckIP(0);
    if (ip->type != T_FILE) {
        Error("Not a file, please use rmdir");
        iput(ip);
        return E_ERROR;
    }
    if (--ip->nlink == 0) {
        itrunc(ip);
    } else {
        iupdate(ip);
    }
    //iput(ip);
    iremove(ip);

    delinum(inum);  //delete file in this uid
    //PrtYes();
    return E_SUCCESS;
}

// cd in pwd
// 0 for success
int _cd(char *name) {
    uint inum = findinum(name);
    if (inum == NINODES) {
        Error("Not found!");
        return E_ERROR;
    }
    //CheckPerm(inum, R);
    inode *ip = iget(inum);
    //CheckIP(0);
    if (ip->type != T_DIR) {
        Error("Not a directory");
        iput(ip);
        return E_ERROR;
    }
    pwd = inum;
    iput(ip);
    return E_SUCCESS;
}

int cmd_cd(char *name) {
    //CheckFmt();
    char *ptr = NULL;
    int backup = pwd;
    if (name[0] == '/') pwd = 0;  // start from root
    char *p = strtok_r(name, "/", &ptr);
    while (p) {
        if (_cd(p) != E_SUCCESS) {  // if not success
            pwd = backup;   // restore the pwd
            return E_ERROR;
        }
        p = strtok_r(NULL, "/", &ptr);
    }
    if (pwd == 0 && uid != 1) {
        pwd = backup;  // not allow to change to root directory
        return E_ERROR;
    }
    //PrtYes();
    return E_SUCCESS;
}

int cmd_rmdir(char *name) {
    //CheckFmt();
    uint inum = findinum(name);
    if (inum == NINODES) {
        Error("Not found!");
        return E_ERROR;
    }
    //CheckPerm(inum, R | W);
    //CheckPerm(pwd, R | W);
    inode *ip = iget(inum);
    //CheckIP(0);
    if (ip->type != T_DIR) {
        Error("Not a dir, please use rm");
        iput(ip);
        return E_ERROR;
    }

    // if directory is not empty
    int empty = 1;
    uchar *buf = malloc(ip->size);
    readi(ip, buf, 0, ip->size);
    direct *de = (direct *)buf;

    int nfile = ip->size / sizeof(direct);
    for (int i = 0; i < nfile; i++) {
        if (de[i].inum == NINODES) continue;  // deleted
        if (strcmp(de[i].name, ".") == 0 || strcmp(de[i].name, "..") == 0) continue;    // .. & .
        empty = 0;
        break;
    }
    free(buf);

    if (!empty) {
        Error("Directory not empty!");
        iput(ip);
        return E_ERROR;
    }

    // ok to delete
    itrunc(ip);
    //iput(ip);
    iremove(ip);
    delinum(inum);
    //PrtYes();
    return E_SUCCESS;
}

int cmd_ls(entry **entries, int *n) {
    //CheckFmt();
    //CheckPerm(pwd, R);
    inode *ip = iget(pwd);  //get current directory
    //CheckIP(0);

    uchar *buf = malloc(ip->size);
    readi(ip, buf, 0, ip->size);
    direct *de = (direct *)buf;

    int nfile = ip->size / sizeof(direct);
    iput(ip);

    *entries = calloc(nfile, sizeof(entry));
    *n = 0;
    for (int i = 0; i < nfile; i++) {
        if (de[i].inum == NINODES) continue;  // deleted
        if (strcmp(de[i].name, ".") == 0 || strcmp(de[i].name, "..") == 0) continue;
        inode *sub = iget(de[i].inum);
        //CheckIP(0);
        (*entries)[*n].type = sub->type;
        strcpy((*entries)[*n].name, de[i].name);
        (*entries)[*n].atime = sub->atime;
        (*entries)[*n].mtime = sub->mtime;
        (*entries)[*n].ctime = sub->ctime;
        (*entries)[*n].size = sub->size;
        (*n)++;
        iput(sub);
    }

    free(buf);

    return E_SUCCESS;
}

int cmd_cat(char *name, uchar **buf, uint *len) {
    //CheckFmt();
    //printf("[cmd_cat] name='%s', len=%zu\n", name, strlen(name));
    uint inum = findinum(name);
    if (inum == NINODES) {
        Error("Not found!");
        return E_ERROR;
    }
    //CheckPerm(inum, R);
    inode *ip = iget(inum);
    //CheckIP(0);
    if (ip->type != T_FILE) {
        Error("Not a file");
        iput(ip);
        return E_ERROR;
    }

    *buf = malloc(ip->size + 1);
    int bytes_read = readi(ip, *buf, 0, ip->size);
    if (bytes_read < 0) {
        Error("Read failed");
        free(*buf);
        iput(ip);
        return E_ERROR;
    }
    *len = bytes_read;  // 记录实际读取的字节数
    (*buf)[ip->size] = '\0';
    //printf("%s\n", (*buf));

    //free(*buf);
    iput(ip);

    return E_SUCCESS;
}

int cmd_w(char *name, uint len, const char *data) {
    //CheckFmt();
    //printf("[cmd_w] name='%s', len=%zu\n", name, strlen(name));
    uint inum = findinum(name);
    if (inum == NINODES) {
        Error("Not found!");
        return E_ERROR;
    }
    //CheckPerm(inum, W);
    inode *ip = iget(inum);
    //CheckIP(0);
    if (ip->type != T_FILE) {
        Error("Not a file");
        iput(ip);
        return E_ERROR;
    }

    if (len > BSIZE * MAXFILEB || len > strlen(data)) {
        Error("Invalid len");
        iput(ip);
        return E_ERROR;
    }

    len = writei(ip, (uchar *)data, 0, len);
    if (len < 0) {
        Error("Write failed");
        iput(ip);
        return E_ERROR;
    }
    
    if (len < ip->size) {   // if the new data is shorter, truncate
        ip->size = len;
        ip->blocks = (ip->size -1) / BSIZE + 1; 
        iupdate(ip);
    }

    iput(ip);
    //PrtYes();

    return E_SUCCESS;
}

int cmd_i(char *name, uint pos, uint len, const char *data) {
    //CheckFmt();
    uint inum = findinum(name);
    if (inum == NINODES) {
        Error("Not found!");
        return E_ERROR;
    }
    //CheckPerm(inum, W);
    inode *ip = iget(inum);
    //CheckIP(0);
    if (ip->type != T_FILE) {
        Error("Not a file");
        iput(ip);
        return E_ERROR;
    }
    if (len + ip->size > MAXFILEB * BSIZE) {
        Error("Too long");
        iput(ip);
        return E_ERROR;
    }

    if (pos >= ip->size) {
        //pos = ip->size;
        writei(ip, (uchar *)data, pos, len);
    } else {
        uchar *buf = malloc(ip->size - pos);
        readi(ip, buf, pos, ip->size - pos);
        writei(ip, (uchar *)data, pos, len);
        writei(ip, buf, pos + len, ip->size - pos);
        free(buf);
    }

    iput(ip);
    //PrtYes();

    return E_SUCCESS;
}

int cmd_d(char *name, uint pos, uint len) {
    //CheckFmt();
    uint inum = findinum(name);
    if (inum == NINODES) {
        Error("Not found!");
        return E_ERROR;
    }
    //CheckPerm(inum, W);
    inode *ip = iget(inum);
    //CheckIP(0);
    if (ip->type != T_FILE) {
        Error("Not a file");
        iput(ip);
        return E_ERROR;
    }
    if (pos + len >= ip->size) {
        ip->size = pos;
        ip->blocks = (ip->size -1) / BSIZE + 1; 
        iupdate(ip);
    } else {
        uint copylen = ip->size - pos - len;
        uchar *buf = malloc(copylen);
        readi(ip, buf, pos + len, copylen);
        writei(ip, buf, pos, copylen);
        ip->size -= len;
        ip->blocks = (ip->size -1) / BSIZE + 1; 
        iupdate(ip);
        free(buf);
    }

    iput(ip);

    return E_SUCCESS;
}

int cmd_login(int auid) {
    if (auid <= 0 || auid >= 1024) {    // uid: 10-bit
        return E_ERROR;
    }
    inode *users_ip = iget(users_inum);
    uchar buf[BSIZE];
    readi(users_ip, buf, 0, users_ip->size);
    userinfo *us = (userinfo *)buf;
    int nuser = users_ip->size / sizeof(userinfo);

    // exist user
    for (int i = 0; i < nuser; i++) {
        if (us[i].uid == auid) {
            uid = auid;
            pwd = us[i].home_inum;
            iput(users_ip);
            return E_SUCCESS;
        }
    }

    // not exist, create a new user
    char home_dir[12];
    sprintf(home_dir, "user%d", auid);
    icreate(T_DIR, home_dir, 0, auid, 0b1111);
    pwd = 0;  // set pwd to root
    uint home_inum = findinum(home_dir);

    userinfo new_user = {auid, home_inum};
    writei(users_ip, (uchar *)&new_user, users_ip->size, sizeof(userinfo));
    uid = auid;
    pwd = home_inum;
    iput(users_ip);
    return E_SUCCESS;
}
