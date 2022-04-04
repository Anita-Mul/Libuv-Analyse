#include <assert.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <uv.h>

void on_read(uv_fs_t *req);

// 句柄的 result 上挂载有结果，每个 task 对应一个 uv_XXX_t，每个 uv_XXX_t.result 上对应 task 执行的结果
/*
返回的result值，<0表示出错，其他值表示成功。但>=0的值在不同的函数中表示的意义不一样，比如在uv_fs_read或者
uv_fs_write中，它代表读取或写入的数据总量，但在uv_fs_open中表示打开的文件描述符
*/
uv_fs_t open_req;
uv_fs_t read_req;
uv_fs_t write_req;

static char buffer[1024];

static uv_buf_t iov;     // 缓冲区的头指针

void on_write(uv_fs_t *req) {
    if (req->result < 0) {
        fprintf(stderr, "Write error: %s\n", uv_strerror((int)req->result));
    }
    else {
        uv_fs_read(uv_default_loop(), &read_req, open_req.result, &iov, 1, -1, on_read);
    }
}

void on_read(uv_fs_t *req) {
    if (req->result < 0) {
        fprintf(stderr, "Read error: %s\n", uv_strerror(req->result));
    }
    else if (req->result == 0) {            // 当读到文件的末尾时(EOF)，result返回0
        uv_fs_t close_req;
        // synchronous
        // 注意这里是 open_req.result
        // uv_fs_close()是同步的
        uv_fs_close(uv_default_loop(), &close_req, open_req.result, NULL);
    }
    else if (req->result > 0) {
        iov.len = req->result;
        uv_fs_write(uv_default_loop(), &write_req, 1, &iov, 1, -1, on_write);
    }
}

// 当获得一个文件描述符到 open_req 中之后的操作
void on_open(uv_fs_t *req) {
    // The request passed to the callback is the same as the one the call setup
    // function was passed.
    assert(req == &open_req);
    
    if (req->result >= 0) {
        iov = uv_buf_init(buffer, sizeof(buffer));
        // uv_fs_t的result域保存了uv_fs_open回调函数打开的文件描述符
        // 如果文件被正确地打开，我们可以开始读取了
        uv_fs_read(uv_default_loop(), &read_req, req->result,
                   &iov, 1, -1, on_read);
    }
    else {
        fprintf(stderr, "error opening file: %s\n", uv_strerror((int)req->result));
    }
}

int main(int argc, char **argv) {
    uv_fs_open(uv_default_loop(), &open_req, argv[1], O_RDONLY, 0, on_open);
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);

    // 函数uv_fs_req_cleanup()在文件系统操作结束后必须要被调用，用来回收在读写中分配的内存
    uv_fs_req_cleanup(&open_req);
    uv_fs_req_cleanup(&read_req);
    uv_fs_req_cleanup(&write_req);
    return 0;
}
