#include "disk.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

#include "log.h"

// global variables
int _ncyl, _nsec, _ttd;
const int blocksize = 512;
int fd;
char *diskfile;
int current_c = 64;
int next_c = 0;

int init_disk(char *filename, int ncyl, int nsec, int ttd) {
    _ncyl = ncyl;
    _nsec = nsec;
    _ttd = ttd;
    printf("Initializing disk: %s, %d Cylinders, %d Sectors per cylinder, Track-to-Track Delay: %d ms\n",
           filename, ncyl, nsec, _ttd);
    // do some initialization...
    long long filesize;
    filesize = ncyl * nsec * blocksize;
    // open file
    fd = open(filename, O_RDWR | O_CREAT, 0644);
    if (fd < 0){
        printf("Error: Could not open file '%s'.\n", filename);
        
        exit(-1);
    }
    // stretch the file
    int result = lseek(fd, filesize-1, SEEK_SET);
    if (result == -1){
        perror("Error calling lseek() to 'stretch' the file");
        close(fd);
        exit(-1); 
    }
    result = write(fd, "", 1);
    if (result != 1){
        perror("Error writing last byte of the file");
        close(fd);
        exit(-1);
    }
    // mmap
    diskfile = (char *) mmap (NULL, filesize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (diskfile == MAP_FAILED){
        close(fd);
        printf("Error: Could not map file.\n");
        exit(-1);
    }
    Log("Disk initialized: %s, %d Cylinders, %d Sectors per cylinder", filename, ncyl, nsec);
    return 0;
}

// all cmd functions return 0 on success
int cmd_i(int *ncyl, int *nsec) {
    // get the disk info
    *ncyl = _ncyl;
    *nsec = _nsec;
    return 0;
}

int cmd_r(int cyl, int sec, char *buf) {
    // read data from disk, store it in buf
    if (cyl >= _ncyl || sec >= _nsec || cyl < 0 || sec < 0) {
        Log("Invalid cylinder or sector");
        return 1;
    }
    next_c = cyl;
    usleep(_ttd * abs(next_c - current_c));
    //current_c = next_c;
    memcpy(buf, &diskfile[blocksize * (cyl * _nsec + sec)], blocksize);

    return 0;
}

int cmd_w(int cyl, int sec, int len, char *data) {
    // write data to disk
    if (cyl >= _ncyl || sec >= _nsec || cyl < 0 || sec < 0) {
        Log("Invalid cylinder or sector");
        return 1;
    }
    if ( len < 0 || len > blocksize) {
        Log("Invalid data length");
        return 1;
    }
    next_c = cyl;
    usleep(_ttd * abs(next_c - current_c));
    //current_c = next_c;
    memcpy(&diskfile[blocksize * (cyl * _nsec + sec)], data, len);
    if (len < blocksize) {
        memset(diskfile + blocksize * (cyl * _nsec + sec) + len, 0, blocksize - len);
    }
    return 0;
}

void close_disk() {
    // close the file
    if (diskfile != MAP_FAILED) {
        long long filesize = (long long)_ncyl * _nsec * blocksize;
        msync(diskfile, filesize, MS_SYNC);
        munmap(diskfile, filesize);
    }
    if (fd >= 0) close(fd);
}
