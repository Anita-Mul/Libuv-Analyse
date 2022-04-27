## unix 域
 - Unix 域一种进程间通信的方式，他类似 socket 通信，但是他是基于单主机的。可以说是单机上的 socket 通信。在 libuv 中，unix 域用 uv_pipe_t 表示
 - unix 域的实现和 tcp 的实现类似。都是基于连接的模式。服务器启动等待连接，客户端去连接。 然后服务器逐个摘下连接的节点进行处理。
#### [`uv_pipe_t`](https://libuv-docs-chinese.readthedocs.io/zh/latest/pipe.html#c.uv_pipe_t "uv_pipe_t") --- 管道句柄
- 管道句柄对Unix上的本地域套接字和Windows上的有名管道提供一个抽象
##### 数据类型
-   `uv_pipe_t`[](https://libuv-docs-chinese.readthedocs.io/zh/latest/pipe.html#c.uv_pipe_t "永久链接至目标")

    管道句柄类型（[结构体具体内容看这里](#heading-9)）
##### API
-   int `uv_pipe_init`([uv_loop_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/loop.html#c.uv_loop_t "uv_loop_t")* *loop*, [uv_pipe_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/pipe.html#c.uv_pipe_t "uv_pipe_t")* *handle*, int *ipc*)[](https://libuv-docs-chinese.readthedocs.io/zh/latest/pipe.html#c.uv_pipe_init "永久链接至目标")

    初始化一个管道句柄。 ipc 参数是一个布尔值指明是否管道将用于在不同进程间传递句柄
-   int `uv_pipe_open`([uv_pipe_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/pipe.html#c.uv_pipe_t "uv_pipe_t")* *handle*, [uv_file](https://libuv-docs-chinese.readthedocs.io/zh/latest/misc.html#c.uv_file "uv_file") *file*)[](https://libuv-docs-chinese.readthedocs.io/zh/latest/pipe.html#c.uv_pipe_open "永久链接至目标")

    打开一个已存在的文件描述符或者句柄作为一个管道
-   int `uv_pipe_bind`([uv_pipe_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/pipe.html#c.uv_pipe_t "uv_pipe_t")* *handle*, const char* *name*)[](https://libuv-docs-chinese.readthedocs.io/zh/latest/pipe.html#c.uv_pipe_bind "永久链接至目标")

    绑定管道到一个文件路径（Unix）或者名字（Windows）
-   void `uv_pipe_connect`([uv_connect_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/stream.html#c.uv_connect_t "uv_connect_t")* *req*, [uv_pipe_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/pipe.html#c.uv_pipe_t "uv_pipe_t")* *handle*, const char* *name*, [uv_connect_cb](https://libuv-docs-chinese.readthedocs.io/zh/latest/stream.html#c.uv_connect_cb "uv_connect_cb") *cb*)[](https://libuv-docs-chinese.readthedocs.io/zh/latest/pipe.html#c.uv_pipe_connect "永久链接至目标")

    连接到Unix域套接字或者有名管道
-   int `uv_pipe_getsockname`(const [uv_pipe_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/pipe.html#c.uv_pipe_t "uv_pipe_t")* *handle*, char* *buffer*, size_t* *size*)[](https://libuv-docs-chinese.readthedocs.io/zh/latest/pipe.html#c.uv_pipe_getsockname "永久链接至目标")

    获取Unix域套接字或者有名管道的名字
-   int `uv_pipe_getpeername`(const [uv_pipe_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/pipe.html#c.uv_pipe_t "uv_pipe_t")* *handle*, char* *buffer*, size_t* *size*)[](https://libuv-docs-chinese.readthedocs.io/zh/latest/pipe.html#c.uv_pipe_getpeername "永久链接至目标")

    获取被句柄连接的Unix域套接字或者有名管道的名字
    
##### example
 - 因为咱们的 unix域 通信过程类似于 socket 通信，所以首先来看一下 socket 通信的基本过程
    ![image.png](https://p3-juejin.byteimg.com/tos-cn-i-k3u1fbpfcp/c4264f41f7fd417b907cd00396429c48~tplv-k3u1fbpfcp-watermark.image?)
###### 服务器端
 - 初始化 uv_pipe_t【[源码实现 uv_pipe_init](#heading-10)】
    ```c
    int main() {
        loop = uv_default_loop();

        uv_pipe_t server;
        uv_pipe_init(loop, &server, 0);
    }
    ```
 - 接着创建 socket，然后 bind 这个 socket【[源码实现 uv_pipe_bind](#heading-11)】
    ```c
    int main() {
        loop = uv_default_loop();

        uv_pipe_t server;
        uv_pipe_init(loop, &server, 0);

        int r;
        // 我们把socket命名为echo.sock，意味着它将会在本地文件夹中被创造。对于stream API来说，本地socekt表现得和tcp的socket差不多
        if ((r = uv_pipe_bind(&server, "echo.sock"))) {
            fprintf(stderr, "Bind error %s\n", uv_err_name(r));
            return 1;
        }
    }
    ```
 - 监听这个 socket，直到有客户端跟他连接【[源码实现 uv_listen](#heading-12)】
     ```c
     int main() {
        loop = uv_default_loop();

        uv_pipe_t server;
        uv_pipe_init(loop, &server, 0);

        int r;
        // 我们把socket命名为echo.sock，意味着它将会在本地文件夹中被创造。对于stream API来说，本地socekt表现得和tcp的socket差不多
        if ((r = uv_pipe_bind(&server, "echo.sock"))) {
            fprintf(stderr, "Bind error %s\n", uv_err_name(r));
            return 1;
        }

        // 把 unix 域对应的文件文件描述符设置为 listen 状态。
        // 开启监听请求的到来，连接的最大个数是 128。有连接时的回调是 on_new_connection
        if ((r = uv_listen((uv_stream_t*) &server, 128, on_new_connection))) {
            fprintf(stderr, "Listen error %s\n", uv_err_name(r));
            return 2;
        }

        // 启动事件循环
        return uv_run(loop, UV_RUN_DEFAULT);
    }
    ```
###### 客户端
 - 初始化 uv_pipe_t【[源码实现 uv_pipe_t](#heading-10)】
 - 跟服务器端连接【[源码实现 on_new_connection](#heading-13)】
##### 服务器端完整代码
 - 启动了一个服务。同主机的进程可以访问（连接）他。
    ```c
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <uv.h>

    uv_loop_t *loop;

    typedef struct {
        uv_write_t req;
        uv_buf_t buf;
    } write_req_t;

    void free_write_req(uv_write_t *req) {
        write_req_t *wr = (write_req_t*) req;
        free(wr->buf.base);
        free(wr);
    }

    void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
      buf->base = malloc(suggested_size);
      buf->len = suggested_size;
    }

    void echo_write(uv_write_t *req, int status) {
        if (status < 0) {
            fprintf(stderr, "Write error %s\n", uv_err_name(status));
        }
        free_write_req(req);
    }

    void echo_read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
        if (nread > 0) {
            write_req_t *req = (write_req_t*) malloc(sizeof(write_req_t));
            req->buf = uv_buf_init(buf->base, nread);
            uv_write((uv_write_t*) req, client, &req->buf, 1, echo_write);
            return;
        }

        if (nread < 0) {
            if (nread != UV_EOF)
                fprintf(stderr, "Read error %s\n", uv_err_name(nread));
            uv_close((uv_handle_t*) client, NULL);
        }

        free(buf->base);
    }
    
    // 有连接到来时的回调
    void on_new_connection(uv_stream_t *server, int status) {
        if (status == -1) {
            // error!
            return;
        }
        
        // 有连接到来，申请一个结构体表示他
        uv_pipe_t *client = (uv_pipe_t*) malloc(sizeof(uv_pipe_t));
        uv_pipe_init(loop, client, 0);
        
        // 把 accept 返回的 fd 记录到 client，client 是用于和客户端通信的结构体
        if (uv_accept(server, (uv_stream_t*) client) == 0) {
            // 注册读事件，等待客户端发送信息过来,
            // alloc_buffer 分配内存保存客户端的发送过来的信息,
            // echo_read 是回调
            uv_read_start((uv_stream_t*) client, alloc_buffer, echo_read);
        }
        else {
            uv_close((uv_handle_t*) client, NULL);
        }
    }

    void remove_sock(int sig) {
        uv_fs_t req;
        // 删除 unix 域对应的路径
        uv_fs_unlink(loop, &req, "echo.sock", NULL);
        // 退出进程
        exit(0);
    }

    /*
        本地socket具有确定的名称，而且是以文件系统上的位置来标示的（例如，unix中socket是文件的一种存在形式），
        那么它就可以用来在不相关的进程间完成通信任务
    */
    int main() {
        loop = uv_default_loop();

        uv_pipe_t server;
        uv_pipe_init(loop, &server, 0);
        
        // 注册 SIGINT 信号的信号处理函数是 remove_sock
        signal(SIGINT, remove_sock);

        int r;
        // 我们把socket命名为echo.sock，意味着它将会在本地文件夹中被创造。对于stream API来说，本地socekt表现得和tcp的socket差不多
        if ((r = uv_pipe_bind(&server, "echo.sock"))) {
            fprintf(stderr, "Bind error %s\n", uv_err_name(r));
            return 1;
        }
        
        // 把 unix 域对应的文件文件描述符设置为 listen 状态。
        // 开启监听请求的到来，连接的最大个数是 128。有连接时的回调是 on_new_connection
        if ((r = uv_listen((uv_stream_t*) &server, 128, on_new_connection))) {
            fprintf(stderr, "Listen error %s\n", uv_err_name(r));
            return 2;
        }
        
        // 启动事件循环
        return uv_run(loop, UV_RUN_DEFAULT);
    }
    ```


## 源码分析
#### uv_pipe_t
 - uv_pipe_t 继承自 handle 和 stream
 - 源码
    ```c
    struct uv_pipe_t {
      // 句柄[handle]相关参数
      void* data;                             
      uv_loop_t* loop;                            /* 所属事件循环 */
      uv_handle_type type;                        /* handle 类型 */
      uv_close_cb close_cb;                       /* 关闭 handle 时的回调 */
      void* handle_queue[2];                      /* 用于插入事件循环的 handle 队列 */
      union {                                                                     
        int fd;                                                                   
        void* reserved[4];                                                        
      } u; 
      uv_handle_t* next_closing;                  /* 用于插入事件循环的 closing 阶段对应的队列 */
      unsigned int flags;                         /* 各种标记 */


      // 流[stream]相关参数                                    
      size_t write_queue_size;                    /* 用户写入流的字节大小，流缓存用户的输入，然后等到可写的时候才做真正的写 */                                             
      uv_alloc_cb alloc_cb;                       /* 分配内存的函数，内存由用户定义，主要用来保存读取的数据 */                                           
      uv_read_cb read_cb;                         /* 读取完成时候执行的回调函数 */
      uv_connect_t *connect_req;                  /* 连接成功后，执行 connect_req 的回调（connect_req 在 uv__xxx_connect 中赋值） */                                
      uv_shutdown_t *shutdown_req;                /* 关闭写端的时候，发送完缓存的数据，执行 shutdown_req 的回调（shutdown_req 在 uv_shutdown 的时候赋值） */                                
      uv__io_t io_watcher;                        /* 流对应的 io 观察者，即文件描述符+一个文件描述符事件触发时执行的回调 */                                
      void* write_queue[2];                       /* 流缓存下来的，待写的数据 */                                
      void* write_completed_queue[2];             /* 已经完成了数据写入的队列 */                                
      uv_connection_cb connection_cb;             /* 完成三次握手后，执行的回调 */                                
      int delayed_error;                          /* 操作流时出错码 */                                
      int accepted_fd;                            /* accept 返回的通信 socket 对应的文件描述符 */                                
      void* queued_fds;                           /* 同上，用于缓存更多的通信 socket 对应的文件描述符 */                                
      UV_STREAM_PRIVATE_PLATFORM_FIELDS           /* 目前为空 */

      int ipc;                                    /* 标记管道是否能在进程间传递 */

      // pipe相关参数
      const char* pipe_fname;                     /* 用于 unix 域通信的文件路径 */
    }
    ```
#### uv_pipe_init
 - 初始化 uv_pipe_t 结构体。刚才已经见过 uv_pipe_t 继承于 stream，uv__stream_init 就是初始化 stream（父类）的字段
 - 源码
    ```c
    int uv_pipe_init(uv_loop_t* loop, uv_pipe_t* handle, int ipc) {
      uv__stream_init(loop, (uv_stream_t*)handle, UV_NAMED_PIPE);
      handle->shutdown_req = NULL;
      handle->connect_req = NULL;
      handle->pipe_fname = NULL;
      handle->ipc = ipc;
      return 0;
    }
    ```
#### uv_pipe_bind - 服务器端
 - 申请一个 socket 套接字，绑定 unix 域路径到 socket 中，绑定了路径后，就可以调用 listen 函数开始监听
 - 源码
    ```c
    // name 是 unix 路径名称
    int uv_pipe_bind(uv_pipe_t* handle, const char* name) {
      struct sockaddr_un saddr;
      const char* pipe_fname;
      int sockfd;
      int err;

      pipe_fname = NULL;

      if (uv__stream_fd(handle) >= 0)
        return UV_EINVAL;

      pipe_fname = uv__strdup(name);
      if (pipe_fname == NULL)
        return UV_ENOMEM;

      name = NULL;

      // unix 域套接字
      err = uv__socket(AF_UNIX, SOCK_STREAM, 0);
      if (err < 0)
        goto err_socket;
      sockfd = err;

      memset(&saddr, 0, sizeof saddr);
      uv__strscpy(saddr.sun_path, pipe_fname, sizeof(saddr.sun_path));
      saddr.sun_family = AF_UNIX;

      // 绑定到路径，tcp 是绑定到 ip 和端口
      if (bind(sockfd, (struct sockaddr*)&saddr, sizeof saddr)) {
        err = UV__ERR(errno);
        /* Convert ENOENT to EACCES for compatibility with Windows. */
        if (err == UV_ENOENT)
          err = UV_EACCES;

        uv__close(sockfd);
        goto err_socket;
      }

      // 已经绑定
      handle->flags |= UV_HANDLE_BOUND;
      handle->pipe_fname = pipe_fname; 
      // 保存 socket fd，用于后面监听
      handle->io_watcher.fd = sockfd;
      return 0;

    err_socket:
      uv__free((void*)pipe_fname);
      return err;
    }
    ```
#### uv_pipe_listen - 服务器端
 - uv_pipe_listen 执行 listen 函数使得 socket 成为监听型的套接字。然后把 socket 对应 的文件描述符和回调封装成 io 观察者。注册到 libuv。等到有读事件到来（有连接到来）。 就会执行 uv__server_io 函数，摘下对应的客户端节点。最后执行 connection_cb 回调。
 - 源码
    ```c
    int uv__pipe_listen(uv_pipe_t* handle, int backlog, uv_connection_cb cb) {
      if (uv__stream_fd(handle) == -1)
        return UV_EINVAL;

      if (handle->ipc)
        return UV_EINVAL;

    #if defined(__MVS__) || defined(__PASE__)
      if (backlog == 0)
        backlog = 1;
      else if (backlog < 0)
        backlog = SOMAXCONN;
    #endif
      // uv__stream_fd(handle)得到 bind 函数中获取的 socket
      if (listen(uv__stream_fd(handle), backlog))
        return UV__ERR(errno);

      // 保存回调，有进程调用 connect 的时候时触发，由 uv__server_io 函数触发
      handle->connection_cb = cb;
      // io 观察者的回调，有进程调用 connect 的时候时触发（io 观察者的 fd 在 init 函数里设置了）
      // uv__server_io 中会调用用户提供的 cb
      handle->io_watcher.cb = uv__server_io;
      // 注册 io 观察者到 libuv，等待连接，即读事件到来
      uv__io_start(handle->loop, &handle->io_watcher, POLLIN);
      return 0;
    }
    ```
#### uv_pipe_connect - 客户端
 - 源码
    ```c
    void uv_pipe_connect(uv_connect_t* req,
                        uv_pipe_t* handle,
                        const char* name,
                        uv_connect_cb cb) {
      struct sockaddr_un saddr;
      int new_sock;
      int err;
      int r;

      // 判断是否已经有 socket 了，没有的话需要申请一个，见下面
      // #define uv__stream_fd(handle) ((handle)->io_watcher.fd)
      new_sock = (uv__stream_fd(handle) == -1);

      // 客户端还没有对应的 socket fd
      if (new_sock) {
        err = uv__socket(AF_UNIX, SOCK_STREAM, 0);
        if (err < 0)
          goto out;
        handle->io_watcher.fd = err;
      }

      // 需要连接的服务器信息。主要是 unix 域路径信息
      memset(&saddr, 0, sizeof saddr);
      uv__strscpy(saddr.sun_path, name, sizeof(saddr.sun_path));
      saddr.sun_family = AF_UNIX;

      // 连接服务器，unix 域路径是 name
      do {
        r = connect(uv__stream_fd(handle),
                    (struct sockaddr*)&saddr, sizeof saddr);
      }
      while (r == -1 && errno == EINTR);

      if (r == -1 && errno != EINPROGRESS) {
        err = UV__ERR(errno);
    #if defined(__CYGWIN__) || defined(__MSYS__)
        if (err == UV_EBADF)
          err = UV_ENOTSOCK;
    #endif
        goto out;
      }

      // 忽略错误处理逻辑
      err = 0;
      // 设置 socket 的可读写属
      if (new_sock) {
        err = uv__stream_open((uv_stream_t*)handle,
                              uv__stream_fd(handle),      // #define uv__stream_fd(handle) ((handle)->io_watcher.fd)
                              UV_HANDLE_READABLE | UV_HANDLE_WRITABLE);
      }

      // 把 io 观察者注册到 libuv，等到连接成功或者可以发送请求
      if (err == 0)
        uv__io_start(handle->loop, &handle->io_watcher, POLLOUT);

    out:
      // 记录错误码，如果有的话
      handle->delayed_error = err;
      // 连接成功时的回调
      handle->connect_req = req;

      uv__req_init(handle->loop, req, UV_CONNECT);
      req->handle = (uv_stream_t*)handle;
      req->cb = cb;
      QUEUE_INIT(&req->queue);

      // 如果连接出错，在 pending 节点会执行 req 对应的回调。错误码是 delayed_error
      if (err)
        uv__io_feed(handle->loop, &handle->io_watcher);
    }
    ```
























