#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <uv.h>

typedef struct {
    uv_write_t req;
    uv_buf_t buf;
} write_req_t;

uv_loop_t *loop;
uv_pipe_t stdin_pipe;
uv_pipe_t stdout_pipe;
uv_pipe_t file_pipe;

/*
当分配函数alloc_buf()返回一个长度为0的缓冲区时，代表它分配内存失败。在这种情况下，读取的回调函数会被错误UV_ENOBUFS唤醒。libuv同时也会继续尝试从流中读取数据，所以如果你想要停止的话，必须明确地调用uv_close()
*/
void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    *buf = uv_buf_init((char*) malloc(suggested_size), suggested_size);
}

void free_write_req(uv_write_t *req) {
    write_req_t *wr = (write_req_t*) req;
    free(wr->buf.base);
    free(wr);
}

void on_stdout_write(uv_write_t *req, int status) {
    free_write_req(req);
}

void on_file_write(uv_write_t *req, int status) {
    free_write_req(req);
}

/*
write_data()开辟了一块地址空间存储从缓冲区读取出来的数据，这块缓存不会被释放，
直到与uv_write()绑定的回调函数执行.为了实现它，我们用结构体write_req_t包裹一
个write request和一个buffer，然后在回调函数中展开它。因为我们复制了一份缓存，
所以我们可以在两个write_data()中独立释放两个缓存。 我们之所以这样做是因为，
两个调用write_data()是相互独立的。为了保证它们不会因为读取速度的原因，由于共
享一片缓冲区而损失掉独立性，所以才开辟了新的两块区域。当然这只是一个简单的例子，
你可以使用更聪明的内存管理方法来实现它，比如引用计数或者缓冲区池等
*/
void write_data(uv_stream_t *dest, size_t size, uv_buf_t buf, uv_write_cb cb) {
    write_req_t *req = (write_req_t*) malloc(sizeof(write_req_t));
    req->buf = uv_buf_init((char*) malloc(size), size);
    // 复制缓冲区中的数据到 req->buf
    memcpy(req->buf.base, buf.base, size);
    // req 是个指针，指向结构体的开头
    uv_write((uv_write_t*) req, (uv_stream_t*)dest, &req->buf, 1, cb);
}

void read_stdin(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    if (nread < 0){
        if (nread == UV_EOF){
            // end of file
            uv_close((uv_handle_t *)&stdin_pipe, NULL);
            uv_close((uv_handle_t *)&stdout_pipe, NULL);
            uv_close((uv_handle_t *)&file_pipe, NULL);
        }
    } else if (nread > 0) {
        // 将缓冲区中的数据写入到 标准输出和文件
        write_data((uv_stream_t *)&stdout_pipe, nread, *buf, on_stdout_write);
        write_data((uv_stream_t *)&file_pipe, nread, *buf, on_file_write);
    }

    // OK to free buffer as write_data copies it.
    // 缓冲区要由我们手动回收
    if (buf->base)
        free(buf->base);
}

int main(int argc, char **argv) {
    loop = uv_default_loop();

    uv_pipe_init(loop, &stdin_pipe, 0);
    // 标准输入
    uv_pipe_open(&stdin_pipe, 0);               // 在使用pipe打开文件时，libuv会默认地以可读和可写的方式打开文件

    uv_pipe_init(loop, &stdout_pipe, 0);
    // 标准输出
    uv_pipe_open(&stdout_pipe, 1);
    
    uv_fs_t file_req;
    int fd = uv_fs_open(loop, &file_req, argv[1], O_CREAT | O_RDWR, 0644, NULL);
    uv_pipe_init(loop, &file_pipe, 0);
    uv_pipe_open(&file_pipe, fd);               // 以 pipe 打开文件就是流的形式【把管道和文件描述符关联起来】

    // 从标准输入读取数据
    // 当调用uv_read_start()后，我们开始监听stdin，当需要新的缓冲区来存储数据时，调用alloc_buffer，在函数read_stdin()中可以定义缓冲区中的数据处理操作
    uv_read_start((uv_stream_t*)&stdin_pipe, alloc_buffer, read_stdin);

    uv_run(loop, UV_RUN_DEFAULT);
    return 0;
}

/*
你的程序在被其他的程序调用的过程中，有意无意地会向pipe写入数据，这
样的话它会很容易被信号SIGPIPE终止掉，你最好在初始化程序的时候加入
这句：
signal(SIGPIPE, SIG_IGN)
*/