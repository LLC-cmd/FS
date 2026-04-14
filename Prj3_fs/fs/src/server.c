#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>
#include <sys/time.h>

#include "log.h"
#include "block.h"
#include "common.h"
#include "fs.h"
#include "tcp_utils.h"

struct timeval startTime, endTime;

// global variales
int ncyl, nsec;
extern int uid;  // current user id
extern uint pwd;  // current working directory inum

int check_login() {
    if (uid == 0) {
        return E_ERROR;
    }
    return E_SUCCESS;
}

int check_uid(){
    if (uid != 1) {
        return E_ERROR;
    }
    return E_SUCCESS;
}

int parse(char *args, char *argv[], int max_args) {
    if (!args || !argv || max_args <= 0) return -1;
    const char *delim = " \r\n\t";
    int argc = 0;
    char *token = strtok(args, delim);
    
    while (token != NULL && argc < max_args) {
        for (int i = strlen(token)-1; i >= 0 && isspace(token[i]); i--) {
            token[i] = '\0';
        }
        argv[argc++] = token;
        token = strtok(NULL, delim);
    }
    return (token == NULL) ? argc : -1;
}

static char* format_full_time(uint timestamp) {
    static char buffer[20]; // 静态缓冲区用于返回 "YYYY-MM-DD HH:MM:SS"
    
    if (timestamp == 0) {
        strcpy(buffer, "0000-00-00 00:00:00");
        return buffer;
    }
    
    time_t raw_time = (time_t)timestamp;
    struct tm *timeinfo = localtime(&raw_time);
    
    if (timeinfo) {
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    } else {
        strcpy(buffer, "INVALID_TIMESTAMP");
    }
    return buffer;
}

// return a negative value to exit
int handle_f(tcp_buffer *wb, char *args, int len) {
    if (check_login() != E_SUCCESS) {
        reply_with_no(wb, "Not logged in", strlen("Not logged in"));
        return 0;
    }
    if (check_uid() != E_SUCCESS) {
        reply_with_no(wb, "No permission to fmt", strlen("No permission to fmt"));
        return 0;
    }
    if (cmd_f(ncyl, nsec) == E_SUCCESS) {
        reply_with_yes(wb, NULL, 0);
    } else {
        reply_with_no(wb, NULL, 0);
    }
    return 0;
}

int handle_mk(tcp_buffer *wb, char *args, int len) {
    if (check_login() != E_SUCCESS) {
        reply_with_no(wb, "Not logged in", strlen("Not logged in"));
        return 0;
    }
    char *argt[4];
    int argc = parse(args, argt, 1);
    char *name = argt[0];
    short mode = 0;
    if (cmd_mk(name, mode) == E_SUCCESS) {
        reply_with_yes(wb, NULL, 0);
    } else {
        reply_with_no(wb, NULL, 0);
    }
    return 0;
}

int handle_mkdir(tcp_buffer *wb, char *args, int len) {
    if (check_login() != E_SUCCESS) {
        reply_with_no(wb, "Not logged in", strlen("Not logged in"));
        return 0;
    }
    char *argt[4];
    int argc = parse(args, argt, 1);
    char *name = argt[0];
    short mode = 0;
    if (cmd_mkdir(name, mode) == E_SUCCESS) {
        reply_with_yes(wb, NULL, 0);
    } else {
        reply_with_no(wb, NULL, 0);
    }
    return 0;
}

int handle_rm(tcp_buffer *wb, char *args, int len) {
    if (check_login() != E_SUCCESS) {
        reply_with_no(wb, "Not logged in", strlen("Not logged in"));
        return 0;
    }
    char *argt[4];
    int argc = parse(args, argt, 1);
    char *name = argt[0];
    if (cmd_rm(name) == E_SUCCESS) {
        reply_with_yes(wb, NULL, 0);
    } else {
        reply_with_no(wb, NULL, 0);
    }
    return 0;
}

int handle_cd(tcp_buffer *wb, char *args, int len) {
    if (check_login() != E_SUCCESS) {
        reply_with_no(wb, "Not logged in", strlen("Not logged in"));
        return 0;
    }
    char *argt[4];
    int argc = parse(args, argt, 1);
    char *name = argt[0];
    if (cmd_cd(name) == E_SUCCESS) {
        reply_with_yes(wb, NULL, 0);
    } else {
        reply_with_no(wb, "Not permitted", strlen("Not permitted"));
    }
    return 0;
}

int handle_rmdir(tcp_buffer *wb, char *args, int len) {
    if (check_login() != E_SUCCESS) {
        reply_with_no(wb, "Not logged in", strlen("Not logged in"));
        return 0;
    }
    char *argt[4];
    int argc = parse(args, argt, 1);
    char *name = argt[0];
    if (cmd_rmdir(name) == E_SUCCESS) {
        reply_with_yes(wb, NULL, 0);
    } else {
        reply_with_no(wb, NULL, 0);
    }
    return 0;
}

int handle_ls(tcp_buffer *wb, char *args, int len) {
    if (check_login() != E_SUCCESS) {
        reply_with_no(wb, "Not logged in", strlen("Not logged in"));
        return 0;
    }
    entry *entries = NULL;
    int n = 0;
    if (cmd_ls(&entries, &n) != E_SUCCESS) {
        reply_with_no(wb, NULL, 0);
        return 0;
    }
    // 1.计算总ls大小
    size_t total_len = 0;
    for (int i = 0; i < n; i++) {
        total_len += snprintf(NULL, 0,
            "type:%-4s\tname:%-12s\tatime:%-19s\tmtime:%-19s\tctime:%-19s\tsize:%u\n",
            (entries[i].type == 1) ? "DIR" : "FILE",  // 直接三元表达式简化
            entries[i].name,
            format_full_time(entries[i].atime),
            format_full_time(entries[i].mtime),
            format_full_time(entries[i].ctime),
            entries[i].size
        );
    }
    // 2. 分配缓冲区（+1 用于终止符）
    char *buf = malloc(total_len + 1);
    if (!buf) {
        reply_with_no(wb, "Memory allocation failed", 25);
        return;
    }
    // 3. 逐行填充缓冲区
    char *pos = buf;  // 动态指针跟踪写入位置
    for (int i = 0; i < n; i++) {
        pos += sprintf(pos,
            "type:%-4s\tname:%-12s\tatime:%-19s\tmtime:%-19s\tctime:%-19s\tsize:%u\n",
            (entries[i].type == 1) ? "DIR" : "FILE",
            entries[i].name,
            format_full_time(entries[i].atime),
            format_full_time(entries[i].mtime),
            format_full_time(entries[i].ctime),
            entries[i].size
        );
    }

    // 4. 发送结果并释放内存
    reply(wb, buf, total_len);
    free(buf);

    free(entries);
    return 0;
}

int handle_cat(tcp_buffer *wb, char *args, int len) {
    if (check_login() != E_SUCCESS) {
        reply_with_no(wb, "Not logged in", strlen("Not logged in"));
        return 0;
    }
    char *argt[4];
    int argc = parse(args, argt, 1);
    char *name = argt[0];

    uchar *buf = NULL;
    uint datalen;
    if (cmd_cat(name, &buf, &datalen) == E_SUCCESS) {
        reply_with_yes(wb, buf, strlen(buf));
        free(buf);
    } else {
        reply_with_no(wb, NULL, 0);
    }
    return 0;
}

int handle_w(tcp_buffer *wb, char *args, int len) {
    if (check_login() != E_SUCCESS) {
        reply_with_no(wb, "Not logged in", strlen("Not logged in"));
        return 0;
    }
    char *argt[4];
    int argc = parse(args, argt, 3);
    char *name = argt[0];
    uint datalen = atoi(argt[1]);
    char *data = argt[2];
    //printf("ready to write to %s %d %s\n", name, datalen, data);
    if (cmd_w(name, datalen, data) == E_SUCCESS) {
        reply_with_yes(wb, NULL, 0);
    } else {
        reply_with_no(wb, NULL, 0);
    }
    return 0;
}

int handle_i(tcp_buffer *wb, char *args, int len) {
    if (check_login() != E_SUCCESS) {
        reply_with_no(wb, "Not logged in", strlen("Not logged in"));
        return 0;
    }
    char *argt[4];
    int argc = parse(args, argt, 4);
    char *name = argt[0];
    uint pos = atoi(argt[1]);
    uint datalen = atoi(argt[2]);
    char *data = argt[3];
    printf("ready to insert to %s %d %d %s\n", name, pos, datalen, data);
    if (cmd_i(name, pos, datalen, data) == E_SUCCESS) {
        reply_with_yes(wb, NULL, 0);
    } else {
        reply_with_no(wb, NULL, 0);
    }
    return 0;
}

int handle_d(tcp_buffer *wb, char *args, int len) {
    if (check_login() != E_SUCCESS) {
        reply_with_no(wb, "Not logged in", strlen("Not logged in"));
        return 0;
    }
    char *argt[4];
    int argc = parse(args, argt, 3);
    char *name = argt[0];
    uint pos = atoi(argt[1]);
    uint datalen = atoi(argt[2]);
    printf("ready to delete to %s %d %d\n", name, pos, datalen);
    if (cmd_d(name, pos, datalen) == E_SUCCESS) {
        reply_with_yes(wb, NULL, 0);
    } else {
        reply_with_no(wb, NULL, 0);
    }
    return 0;
}

int handle_e(tcp_buffer *wb, char *args, int len) {
    char *buf = "Bye!\n";
    reply(wb, buf, strlen(buf));
    Log("Exit");
    return -1;
}

int handle_login(tcp_buffer *wb, char *args, int len) {
    char *argt[4];
    int argc = parse(args, argt, 3);
    int auid = atoi(argt[0]);
    char *buf = malloc(32);
    sprintf(buf, "Hello uid: %d!", auid);
    if (cmd_login(auid) == E_SUCCESS) {
        reply_with_yes(wb, buf, strlen(buf));
    } else {
        reply_with_no(wb, NULL, 0);
    }
    free(buf);
    return 0;
}

static struct {
    const char *name;
    int (*handler)(tcp_buffer *wb, char *, int);
} cmd_table[] = {{"f", handle_f},        {"mk", handle_mk},       {"mkdir", handle_mkdir}, {"rm", handle_rm},
                 {"cd", handle_cd},      {"rmdir", handle_rmdir}, {"ls", handle_ls},       {"cat", handle_cat},
                 {"w", handle_w},        {"i", handle_i},         {"d", handle_d},         {"e", handle_e},
                 {"login", handle_login}};


#define NCMD (sizeof(cmd_table) / sizeof(cmd_table[0]))

void on_connection(int id) {
    // some code that are executed when a new client is connected
}

int on_recv(int id, tcp_buffer *wb, char *msg, int len) {
    char *p = strtok(msg, " \r\n");
    char *args;
    int ret = 1;
    for (int i = 0; i < NCMD; i++)
        if (p && strcmp(p, cmd_table[i].name) == 0) {
            gettimeofday(&startTime, NULL);
            ret = cmd_table[i].handler(wb, p + strlen(p) + 1, len - strlen(p) - 1);
            gettimeofday(&endTime, NULL);
            long elapsedTime = (endTime.tv_sec - startTime.tv_sec) * 1000 + (endTime.tv_usec - startTime.tv_usec) / 1000;
            printf("Command executed in %ld ms\n", elapsedTime);
            break;
        }
    if (ret == 1) {
        static char unk[] = "Unknown command";
        buffer_append(wb, unk, sizeof(unk));
    }
    if (ret < 0) {
        return -1;
    }
    return 0;
}

void cleanup(int id) {
    // some code that are executed when a client is disconnected
}

FILE *log_file;

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <Port:BDC> <Port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int dport = atoi(argv[1]);
    int fport = atoi(argv[2]);

    log_init("fs.log");
    assert(BSIZE % sizeof(dinode) == 0);
    inode_cache_init();
    block_init(dport);
    // get disk info and store in global variables
    get_disk_info(&ncyl, &nsec);

    sbinit();
    if(sb.magic != MAGIC) {
        cmd_f(ncyl, nsec);  // initialize the filesystem
    }
    //printf("reach here!\n");
    // command
    tcp_server server = server_init(fport, 1, on_connection, on_recv, cleanup);
    server_run(server);
    inode_cache_free();
    // never reached
    log_close();
}
