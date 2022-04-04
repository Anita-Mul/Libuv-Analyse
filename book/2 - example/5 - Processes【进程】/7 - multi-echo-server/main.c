#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

/*
    最酷的事情是本地socket可以传递文件描述符，也就是说进程间可以交换文件描述符。这样就允许进程将它们的I/O传递给其他进程。
    它的应用场景包括，负载均衡服务器，分派工作进程等，各种可以使得cpu使用最优化的应用。libuv当前只支持通过管道传输 TCP sockets
    或者其他的pipes。

    为了展示这个功能，我们将来实现一个由循环中的工人进程处理client端请求，的这么一个echo服务器程序
*/

uv_loop_t *loop;

// child_worker结构包裹着进程，和连接主进程和各个独立进程的管道
struct child_worker {
    uv_process_t req;
    uv_process_options_t options;
    uv_pipe_t pipe;
} *workers;

int round_robin_counter;
int child_worker_count;

uv_buf_t dummy_buf;
char worker_path[500];

void close_process_handle(uv_process_t *req, int64_t exit_status, int term_signal) {
    fprintf(stderr, "Process exited with status %" PRId64 ", signal %d\n", exit_status, term_signal);
    uv_close((uv_handle_t*) req, NULL);
}

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  buf->base = malloc(suggested_size);
  buf->len = suggested_size;
}

void on_new_connection(uv_stream_t *server, int status) {
    if (status == -1) {
        // error!
        return;
    }

    uv_tcp_t *client = (uv_tcp_t*) malloc(sizeof(uv_tcp_t));
    uv_tcp_init(loop, client);
    // 接收了client端的socket，然后把它传递给worker环中的下一个可用的worker进程
    if (uv_accept(server, (uv_stream_t*) client) == 0) {
        uv_write_t *write_req = (uv_write_t*) malloc(sizeof(uv_write_t));
        dummy_buf = uv_buf_init("a", 1);
        struct child_worker *worker = &workers[round_robin_counter];
        uv_write2(write_req, (uv_stream_t*) &worker->pipe, &dummy_buf, 1, (uv_stream_t*) client, NULL);
        round_robin_counter = (round_robin_counter + 1) % child_worker_count;
      
    }
    uv_close((uv_handle_t*) client, NULL);
    
}

void setup_workers() {
    size_t path_size = 500;
    uv_exepath(worker_path, &path_size);
    strcpy(worker_path + (strlen(worker_path) - strlen("multi-echo-server")), "worker");
    fprintf(stderr, "Worker path: %s\n", worker_path);

    char* args[2];
    args[0] = worker_path;
    args[1] = NULL;

    round_robin_counter = 0;

    // ...

    // launch same number of workers as number of CPUs
    // 使用 uv_cpu_info 函数获取到当前的 cpu 的核心个数，所以我们也能启动一样数目的worker进程
    uv_cpu_info_t *info;
    int cpu_count;
    uv_cpu_info(&info, &cpu_count);
    uv_free_cpu_info(info, cpu_count);

    child_worker_count = cpu_count;

    workers = calloc(sizeof(struct child_worker), cpu_count);
    // worker进程被启动，等待着文件描述符被写入到他们的标准输入中
    while (cpu_count--) {
        struct child_worker *worker = &workers[cpu_count];
        // 将uv_pipe_init的ipc参数设置为1，标准输出
        uv_pipe_init(loop, &worker->pipe, 1);

        uv_stdio_container_t child_stdio[3];
        child_stdio[0].flags = UV_CREATE_PIPE | UV_READABLE_PIPE;
        child_stdio[0].data.stream = (uv_stream_t*) &worker->pipe;
        child_stdio[1].flags = UV_IGNORE;
        child_stdio[2].flags = UV_INHERIT_FD;        // 插入文件描述符
        child_stdio[2].data.fd = 2;

        worker->options.stdio = child_stdio;
        worker->options.stdio_count = 3;

        worker->options.exit_cb = close_process_handle;
        worker->options.file = args[0];
        worker->options.args = args;

        // 初始化进程描述符并且启动进程
        uv_spawn(loop, &worker->req, &worker->options); 
        fprintf(stderr, "Started worker %d\n", worker->req.pid);
    }
}

int main() {
    loop = uv_default_loop();

    setup_workers();

    uv_tcp_t server;
    uv_tcp_init(loop, &server);

    struct sockaddr_in bind_addr;
    uv_ip4_addr("0.0.0.0", 7000, &bind_addr);
    uv_tcp_bind(&server, (const struct sockaddr *)&bind_addr, 0);

    int r;
    if ((r = uv_listen((uv_stream_t*) &server, 128, on_new_connection))) {
        fprintf(stderr, "Listen error %s\n", uv_err_name(r));
        return 2;
    }
    return uv_run(loop, UV_RUN_DEFAULT);
}

