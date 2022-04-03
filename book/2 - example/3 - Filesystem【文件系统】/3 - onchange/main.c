#include <stdio.h>
#include <stdlib.h>

#include <uv.h>

/*
    所有的现代操作系统都会提供相应的API来监视文件和文件夹的变化(如Linux的inotify，Darwin的FSEvents，
    BSD的kqueue，Windows的ReadDirectoryChangesW， Solaris的event ports)。libuv同样包括了这样的
    文件监视库。这是libuv中很不协调的部分，因为在跨平台的前提上，实现这个功能很难。为了更好地说明，
    我们现在来写一个监视文件变化的命令：
*/
uv_loop_t *loop;
const char *command;

/*
    1.uv_fs_event_t *handle-句柄。里面的path保存了发生改变的文件的地址。
    2.const char *filename-如果目录被监视，它代表发生改变的文件名。只在Linux和Windows上不为null，在其他平台上可能为null。
    3.int flags -UV_RENAME名字改变，UV_CHANGE内容改变之一，或者他们两者的按位或的结果(|)。
    4.int status－当前为0.
*/
void run_command(uv_fs_event_t *handle, const char *filename, int events, int status) {
    char path[1024];
    size_t size = 1023;
    // Does not handle error if path is longer than 1023.
    uv_fs_event_getpath(handle, path, &size);
    path[size] = '\0';

    fprintf(stderr, "Change detected in %s: ", path);
    if (events & UV_RENAME)
        fprintf(stderr, "renamed");
    if (events & UV_CHANGE)
        fprintf(stderr, "changed");

    fprintf(stderr, " %s\n", filename ? filename : "");
    system(command);
}

int main(int argc, char **argv) {
    if (argc <= 2) {
        fprintf(stderr, "Usage: %s <command> <file1> [file2 ...]\n", argv[0]);
        return 1;
    }

    loop = uv_default_loop();
    command = argv[1];

    while (argc-- > 2) {
        fprintf(stderr, "Adding watch on %s\n", argv[argc]);
        uv_fs_event_t *fs_event_req = malloc(sizeof(uv_fs_event_t));
        uv_fs_event_init(loop, fs_event_req);
        // The recursive flag watches subdirectories too.
        /*
            函数uv_fs_event_start()的第三个参数是要监视的文件或文件夹。最后一个参数，flags，可以是：
                UV_FS_EVENT_WATCH_ENTRY = 1,
                UV_FS_EVENT_STAT = 2,
                UV_FS_EVENT_RECURSIVE = 4
            
            UV_FS_EVENT_WATCH_ENTRY和UV_FS_EVENT_STAT不做任何事情(至少目前是这样)，UV_FS_EVENT_RECURSIVE
            可以在支持的系统平台上递归地监视子文件夹。
        */
        uv_fs_event_start(fs_event_req, run_command, argv[argc], UV_FS_EVENT_RECURSIVE);
    }

    return uv_run(loop, UV_RUN_DEFAULT);
}
