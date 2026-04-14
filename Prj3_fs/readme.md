This is a basic file system.
Dir disk and fs are stores two main parts of the implement.
Codes are written in the subdir src, including:
disk/src: disk.c server.c client.c main.c
fs/src: block.c inode.c fs.c server.c client.c main.c
In disk, BDS is the disk server.
In fs, FS is the fs server, FC is the fs client.
While using, run:
./BDS mydisk cyl sec ttd dport
./FS dport fport
./FC fport
User login and give commands in FC, the file system give reply.
In addition to the basic requirements, the fs also implement inode-cache to modify.