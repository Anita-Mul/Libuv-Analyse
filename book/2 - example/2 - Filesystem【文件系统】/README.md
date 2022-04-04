## Filesystem
 - [官方文档](https://libuv-docs-chinese.readthedocs.io/zh/latest/fs.html)
 - libuv 提供的文件操作和 socket operations 并不相同. 套接字操作使用了操作系统本身提供了非阻塞操作, 而文件操作内部使用了阻塞函数, 但是 libuv 是在线程池中调用这些函数, 并在应用程序需要交互时通知在事件循环中注册的监视器.
 - 所有的文件操作函数都有两种形式 - 同步 synchronous 和 异步 asynchronous.





## Reading/Writing files
 - `int uv_fs_open(uv_loop_t* loop, uv_fs_t* req, const char* path, int flags, int mode, uv_fs_cb cb)`
    获得文件描述符
 - `int uv_fs_close(uv_loop_t* loop, uv_fs_t* req, uv_file file, uv_fs_cb cb)`
    关闭文件描述符
 - `void callback(uv_fs_t* req);`
    文件系统的回调函数
#### uvcat/main.c - opening a file





## Filesystem operations
 - 返回的result值，<0表示出错，其他值表示成功。但>=0的值在不同的函数中表示的意义不一样，比如在uv_fs_read或者uv_fs_write中，它代表读取或写入的数据总量，但在uv_fs_open中表示打开的文件描述符
    ````c
    UV_EXTERN int uv_fs_close(uv_loop_t* loop,
                            uv_fs_t* req,
                            uv_file file,
                            uv_fs_cb cb);
    UV_EXTERN int uv_fs_open(uv_loop_t* loop,
                            uv_fs_t* req,
                            const char* path,
                            int flags,
                            int mode,
                            uv_fs_cb cb);
    UV_EXTERN int uv_fs_read(uv_loop_t* loop,
                            uv_fs_t* req,
                            uv_file file,
                            const uv_buf_t bufs[],
                            unsigned int nbufs,
                            int64_t offset,
                            uv_fs_cb cb);
    UV_EXTERN int uv_fs_unlink(uv_loop_t* loop,
                            uv_fs_t* req,
                            const char* path,
                            uv_fs_cb cb);
    UV_EXTERN int uv_fs_write(uv_loop_t* loop,
                            uv_fs_t* req,
                            uv_file file,
                            const uv_buf_t bufs[],
                            unsigned int nbufs,
                            int64_t offset,
                            uv_fs_cb cb);
    UV_EXTERN int uv_fs_mkdir(uv_loop_t* loop,
                            uv_fs_t* req,
                            const char* path,
                            int mode,
                            uv_fs_cb cb);
    UV_EXTERN int uv_fs_mkdtemp(uv_loop_t* loop,
                                uv_fs_t* req,
                                const char* tpl,
                                uv_fs_cb cb);
    UV_EXTERN int uv_fs_rmdir(uv_loop_t* loop,
                            uv_fs_t* req,
                            const char* path,
                            uv_fs_cb cb);
    UV_EXTERN int uv_fs_scandir(uv_loop_t* loop,
                                uv_fs_t* req,
                                const char* path,
                                int flags,
                                uv_fs_cb cb);
    UV_EXTERN int uv_fs_scandir_next(uv_fs_t* req,
                                    uv_dirent_t* ent);
    UV_EXTERN int uv_fs_stat(uv_loop_t* loop,
                            uv_fs_t* req,
                            const char* path,
                            uv_fs_cb cb);
    UV_EXTERN int uv_fs_fstat(uv_loop_t* loop,
                            uv_fs_t* req,
                            uv_file file,
                            uv_fs_cb cb);
    UV_EXTERN int uv_fs_rename(uv_loop_t* loop,
                            uv_fs_t* req,
                            const char* path,
                            const char* new_path,
                            uv_fs_cb cb);
    UV_EXTERN int uv_fs_fsync(uv_loop_t* loop,
                            uv_fs_t* req,
                            uv_file file,
                            uv_fs_cb cb);
    UV_EXTERN int uv_fs_fdatasync(uv_loop_t* loop,
                                uv_fs_t* req,
                                uv_file file,
                                uv_fs_cb cb);
    UV_EXTERN int uv_fs_ftruncate(uv_loop_t* loop,
                                uv_fs_t* req,
                                uv_file file,
                                int64_t offset,
                                uv_fs_cb cb);
    UV_EXTERN int uv_fs_sendfile(uv_loop_t* loop,
                                uv_fs_t* req,
                                uv_file out_fd,
                                uv_file in_fd,
                                int64_t in_offset,
                                size_t length,
                                uv_fs_cb cb);
    UV_EXTERN int uv_fs_access(uv_loop_t* loop,
                            uv_fs_t* req,
                            const char* path,
                            int mode,
                            uv_fs_cb cb);
    UV_EXTERN int uv_fs_chmod(uv_loop_t* loop,
                            uv_fs_t* req,
                            const char* path,
                            int mode,
                            uv_fs_cb cb);
    UV_EXTERN int uv_fs_utime(uv_loop_t* loop,
                            uv_fs_t* req,
                            const char* path,
                            double atime,
                            double mtime,
                            uv_fs_cb cb);
    UV_EXTERN int uv_fs_futime(uv_loop_t* loop,
                            uv_fs_t* req,
                            uv_file file,
                            double atime,
                            double mtime,
                            uv_fs_cb cb);
    UV_EXTERN int uv_fs_lstat(uv_loop_t* loop,
                            uv_fs_t* req,
                            const char* path,
                            uv_fs_cb cb);
    UV_EXTERN int uv_fs_link(uv_loop_t* loop,
                            uv_fs_t* req,
                            const char* path,
                            const char* new_path,
                            uv_fs_cb cb);
    ````









## Buffers and Streams【uv_stream_t --- 流句柄】
 - [官方文档   uv_stream_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/stream.html)
 - TCP套接字，UDP套接字，管道对于文件I/O和IPC来说，都可以看成是流stream(uv_stream_t)的子类
    ```c
    // 当uv_read_start()一旦被调用，libuv会保持从流中持续地读取数据，直到uv_read_stop()被调用
    int uv_read_start(uv_stream_t*, uv_alloc_cb alloc_cb, uv_read_cb read_cb);
    int uv_read_stop(uv_stream_t*);
    int uv_write(uv_write_t* req, uv_stream_t* handle,
                    const uv_buf_t bufs[], unsigned int nbufs, uv_write_cb cb);
    ```
 - `buffer-uv_buffer_t`。它包含了指向数据的开始地址的指针(`uv_buf_t.base`)和buffer的长度(`uv_buf_t.len`)这两个信息
 - `uv_pipe_t`: 它可以将本地文件转换为流（stream）的形态。在使用pipe打开文件时，libuv会默认地以可读和可写的方式打开文件





## File change events【uv_fs_event_t --- FS Event handle】
 - 监视文件变化
 - [官方文档](https://libuv-docs-chinese.readthedocs.io/zh/latest/fs_event.html#c.uv_fs_event_start)