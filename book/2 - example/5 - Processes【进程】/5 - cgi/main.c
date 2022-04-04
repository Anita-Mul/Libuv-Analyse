#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

// uv_loop_t 循环事件类型
uv_loop_t *loop;
// 进程句柄类型
uv_process_t child_req;
// 生成进程的选项（传递给 uv_spawn() 的）
/*
    typedef struct uv_process_options_s {
        uv_exit_cb exit_cb;
        const char* file;
        char** args;
        char** env;
        const char* cwd;
        unsigned int flags;
        int stdio_count;
        uv_stdio_container_t* stdio;
        uv_uid_t uid;
        uv_gid_t gid;
    } uv_process_options_t;
*/
uv_process_options_t options;

// 进程退出的函数
void cleanup_handles(uv_process_t *req, int64_t exit_status, int term_signal) {
    fprintf(stderr, "Process exited with status %" PRId64 ", signal %d\n", exit_status, term_signal);
    uv_close((uv_handle_t*) req->data, NULL);    // client
    uv_close((uv_handle_t*) req, NULL);
}

void invoke_cgi_script(uv_tcp_t *client) {
    size_t size = 500;
    char path[size];
    uv_exepath(path, &size);
    strcpy(path + (strlen(path) - strlen("cgi")), "tick");

    char* args[2];
    args[0] = path;         // ...../tick.c
    args[1] = NULL;

    /* ... finding the executable path and setting up arguments ... */
    // 每一个client在中断连接后，都会被发送10个tick
    /*
        cgi的stdout被绑定到socket上，所以无论tick脚本程序打印什么，都会发送到
        client端。通过使用进程，我们能够很好地处理读写并发操作，而且用起来也很
        方便。但是要记得这么做，是很浪费资源的
    */
    options.stdio_count = 3;

    uv_stdio_container_t child_stdio[3];
    child_stdio[0].flags = UV_IGNORE;
    child_stdio[1].flags = UV_INHERIT_STREAM;                   // 子进程会把这个stream当成是标准的I/O
    child_stdio[1].data.stream = (uv_stream_t*) client;         // 重置孩子的输出缓冲区
    child_stdio[2].flags = UV_IGNORE;

    options.stdio = child_stdio;
    options.exit_cb = cleanup_handles;
    options.file = args[0];
    options.args = args;

    // Set this so we can close the socket after the child process exits.
    child_req.data = (void*) client;

    int r;
    // 初始化进程描述符并启动进程。如果进程成功生成，这个函数返回0。
    if ((r = uv_spawn(loop, &child_req, &options))) {
        fprintf(stderr, "%s\n", uv_strerror(r));
        return;
    }
}

/*
    当流服务器接收到新来的连接时的回调函数。 
    用户能够通过调用 uv_accept() 来接受连接。 status 若成功将是0，否则 < 0
*/
void on_new_connection(uv_stream_t *server, int status) {
    if (status == -1) {
        // error!
        return;
    }

    uv_tcp_t *client = (uv_tcp_t*) malloc(sizeof(uv_tcp_t));
    uv_tcp_init(loop, client);
    
    if (uv_accept(server, (uv_stream_t*) client) == 0) {
        // 接受了连接，并把socket（流）传递给invoke_cgi_script
        invoke_cgi_script(client);
    }
    else {
        // 请求句柄关闭
        uv_close((uv_handle_t*) client, NULL);
    }
}

int main() {
    loop = uv_default_loop();

    uv_tcp_t server;
    // 初始化句柄
    uv_tcp_init(loop, &server);

    struct sockaddr_in bind_addr;
    uv_ip4_addr("0.0.0.0", 7000, &bind_addr);
    uv_tcp_bind(&server, (const struct sockaddr *)&bind_addr, 0);

    // 128 指内核可能排队的连接数
    // 发生错误返回错误号，成功返回 0
    int r = uv_listen((uv_stream_t*) &server, 128, on_new_connection);
    
    if (r) {
        fprintf(stderr, "Listen error %s\n", uv_err_name(r));
        return 1;
    }

    // 以不同的模式运行事件循环
    return uv_run(loop, UV_RUN_DEFAULT);
}
