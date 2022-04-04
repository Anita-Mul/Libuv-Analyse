## Processes【uv_process_t --- 进程句柄】
 - 事件驱动的缺点
    1. 不能很好地利用现代多核计算机的优势
 - [官方文档](https://libuv-docs-chinese.readthedocs.io/zh/latest/process.html)




## Spawning child processes
#### spawn/main.c
 - 编译运行
   `gcc main.c -L/usr/local/lib/ -luv -o main`




## Changing process parameters
#### Change execution directory
 - 设置`uv_process_options_t.cwd`
#### Set environment variables
 - 设置`uv_process_options_t.env`
   以null为结尾的字符串数组，其中每一个字符串的形式都是VAR=VALUE
   如果子进程想要继承父进程的环境变量，就将uv_process_options_t.env设为null
#### Option flags
 - 设置 `uv_process_options_t.flags`，可以定义子进程的行为
   >  - UV_PROCESS_SETUID-将子进程的执行用户id（UID）设置为uv_process_options_t.uid中的值。
   >  - UV_PROCESS_SETGID-将子进程的执行组id(GID)设置为uv_process_options_t.gid中的值。
   只有在unix系的操作系统中支持设置用户id和组id，在windows下设置会失败，uv_spawn会返回UV_ENOTSUP。
   >  - UV_PROCESS_WINDOWS_VERBATIM_ARGUMENTS-在windows上，uv_process_options_t.args参数不要用引号包裹。此标记对unix无效。
   > - UV_PROCESS_DETACHED-在新会话(session)中启动子进程，这样子进程就可以在父进程退出后继续进行。请看下面的例子：
#### 例：Detaching processes
 - 使用标识UV_PROCESS_DETACHED可以启动守护进程(daemon)，或者是使得子进程从父进程中独立出来，这样父进程的退出就不会影响到它
 - 编译运行
   `gcc main.c -L/usr/local/lib/ -luv -o main`





## Sending signals to processes
 - SIGTERM，SIGINT和SIGKILL都会导致进程的中断
 - uv_err_t uv_kill(int pid, int signum);
   发送指定的信号到给定的PID
 - uv_process_kill(uv_process_t* handle, int signum)
   1. 发送指定的信号到给定的进程句柄
   2. 对于用libuv启动的进程，应该使用uv_process_kill终止，它会以uv_process_t作为第一个参数，而不是pid。当使用uv_process_kill后，记得使用uv_close关闭uv_process_t




## uv_signal_t --- 信号句柄
 - [官方文档](https://libuv-docs-chinese.readthedocs.io/zh/latest/signal.html)
 - 使用`uv_signal_init`初始化`handle（uv_signal_t）`，然后将它与loop关联。为了使用handle监听特定的信号，使用`uv_signal_start()`函数。每一个handle只能与一个信号关联，后续的`uv_signal_start`会覆盖前面的关联
#### signal/main.c
 - 编译运行
   `gcc main.c -L/usr/local/lib/ -luv -o main`





## Child Process I/O
 - 一个正常的新产生的进程都有自己的一套文件描述符映射表，例如0，1，2分别对应stdin，stdout和stderr。有时候父进程想要将自己的文件描述符映射表分享给子进程，libuv提供了继承文件描述符的功能。
#### proc-streams/main.c
 - 编译运行
   `gcc main.c -L/usr/local/lib/ -luv -o main`
#### cgi/main.c
 - 编译运行
   `gcc main.c -L/usr/local/lib/ -luv -o main`





## Pipes【uv_pipe_t --- 管道句柄】
 - uv_pipe_t背后有unix本地socket或者windows实名管道的支持，可以实现多进程间的通信
#### Parent-child IPC
 - 父进程与子进程可以通过单工或者双工管道通信，获得管道可以通过设置uv_stdio_container_t.flags为UV_CREATE_PIPE，UV_READABLE_PIPE或者UV_WRITABLE_PIPE的按位或的值。上述的读／写标记是对于子进程而言的
#### pipe-echo-server/main.c
 - 编译运行
   `gcc main.c -L/usr/local/lib/ -luv -o main`
#### Sending file descriptors over pipes
 - 本地socket可以传递文件描述符，也就是说进程间可以交换文件描述符。这样就允许进程将它们的I/O传递给其他进程
#### multi-echo-server/main.c
 - 实现由循环中的工人进程处理client端请求的一个echo服务器程序

