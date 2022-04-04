#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include <uv.h>

uv_loop_t *loop;
uv_process_t child_req;
uv_process_options_t options;

void on_exit(uv_process_t *req, int64_t exit_status, int term_signal) {
    fprintf(stderr, "Process exited with status %" PRId64 ", signal %d\n", exit_status, term_signal);
    uv_close((uv_handle_t*) req, NULL);
}

/*
    一个正常的新产生的进程都有自己的一套文件描述符映射表，例如0，1，2分别对应stdin，stdout和stderr。有时候父
    进程想要将自己的文件描述符映射表分享给子进程。例如，你的程序启动了一个子命令，并且把所有的错误信息输出到log
    文件中，但是不能使用stdout。因此，你想要使得你的子进程和父进程一样，拥有stderr。在这种情形下，libuv提供了
    继承文件描述符的功能
*/
int main() {
    loop = uv_default_loop();

    size_t size = 500;
    char path[size];
    uv_exepath(path, &size);
    strcpy(path + (strlen(path) - strlen("proc-streams")), "test");

    char* args[2];
    args[0] = path;
    args[1] = NULL;

    /* ... */

    /*
        因为我们想要传递一个已经存在的文件描述符，所以使用UV_INHERIT_FD。
        因此，fd被设为stderr

        如果你不打算使用，可以设置为UV_IGNORE。如果与stdio中对应的前三个文件描述符被标记为UV_IGNORE，那么它们会被重定向到/dev/null。
        因为我们想要传递一个已经存在的文件描述符，所以使用UV_INHERIT_FD
    */
    options.stdio_count = 3;
    uv_stdio_container_t child_stdio[3];
    child_stdio[0].flags = UV_IGNORE;
    child_stdio[1].flags = UV_IGNORE;
    child_stdio[2].flags = UV_INHERIT_FD;
    child_stdio[2].data.fd = 2;
    options.stdio = child_stdio;

    options.exit_cb = on_exit;
    options.file = args[0];
    options.args = args;

    int r;
    if ((r = uv_spawn(loop, &child_req, &options))) {
        fprintf(stderr, "%s\n", uv_strerror(r));
        return 1;
    }

    return uv_run(loop, UV_RUN_DEFAULT);
}
