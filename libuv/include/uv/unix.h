/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef UV_UNIX_H
#define UV_UNIX_H

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>  /* MAXHOSTNAMELEN on Solaris */

#include <termios.h>
#include <pwd.h>

#if !defined(__MVS__)
#include <semaphore.h>
#include <sys/param.h> /* MAXHOSTNAMELEN on Linux and the BSDs */
#endif
#include <pthread.h>
#include <signal.h>

#include "uv/threadpool.h"

#if defined(__linux__)
# include "uv/linux.h"
#elif defined (__MVS__)
# include "uv/os390.h"
#elif defined(__PASE__)  /* __PASE__ and _AIX are both defined on IBM i */
# include "uv/posix.h"  /* IBM i needs uv/posix.h, not uv/aix.h */
#elif defined(_AIX)
# include "uv/aix.h"
#elif defined(__sun)
# include "uv/sunos.h"
#elif defined(__APPLE__)
# include "uv/darwin.h"
#elif defined(__DragonFly__)       || \
      defined(__FreeBSD__)         || \
      defined(__FreeBSD_kernel__)  || \
      defined(__OpenBSD__)         || \
      defined(__NetBSD__)
# include "uv/bsd.h"
#elif defined(__CYGWIN__) || \
      defined(__MSYS__)   || \
      defined(__HAIKU__)  || \
      defined(__QNX__)    || \
      defined(__GNU__)
# include "uv/posix.h"
#endif

#ifndef NI_MAXHOST
# define NI_MAXHOST 1025
#endif

#ifndef NI_MAXSERV
# define NI_MAXSERV 32
#endif

#ifndef UV_IO_PRIVATE_PLATFORM_FIELDS
# define UV_IO_PRIVATE_PLATFORM_FIELDS /* empty */
#endif

struct uv__io_s;
struct uv_loop_s;

typedef void (*uv__io_cb)(struct uv_loop_s* loop,
                          struct uv__io_s* w,
                          unsigned int events);
typedef struct uv__io_s uv__io_t;

struct uv__io_s {
  /* 事件触发后的回调 */
  uv__io_cb cb;
  /* 用于插入队列 */
  void* pending_queue[2];
  void* watcher_queue[2];
  /* 保存本次感兴趣的事件，在插入 io 观察者队列时设置 */
  unsigned int pevents; /* Pending event mask i.e. mask at next tick. */
  /* 保存当前感兴趣的事件 */
  unsigned int events;  /* Current event mask. */
  int fd;
  UV_IO_PRIVATE_PLATFORM_FIELDS
};

#ifndef UV_PLATFORM_SEM_T
# define UV_PLATFORM_SEM_T sem_t
#endif

#ifndef UV_PLATFORM_LOOP_FIELDS
# define UV_PLATFORM_LOOP_FIELDS /* empty */
#endif

#ifndef UV_PLATFORM_FS_EVENT_FIELDS
# define UV_PLATFORM_FS_EVENT_FIELDS /* empty */
#endif

#ifndef UV_STREAM_PRIVATE_PLATFORM_FIELDS
# define UV_STREAM_PRIVATE_PLATFORM_FIELDS /* empty */
#endif

/* Note: May be cast to struct iovec. See writev(2). */
typedef struct uv_buf_t {
  char* base;
  size_t len;
} uv_buf_t;

typedef int uv_file;
typedef int uv_os_sock_t;
typedef int uv_os_fd_t;
typedef pid_t uv_pid_t;

#define UV_ONCE_INIT PTHREAD_ONCE_INIT

typedef pthread_once_t uv_once_t;
typedef pthread_t uv_thread_t;
typedef pthread_mutex_t uv_mutex_t;
typedef pthread_rwlock_t uv_rwlock_t;
typedef UV_PLATFORM_SEM_T uv_sem_t;
typedef pthread_cond_t uv_cond_t;
typedef pthread_key_t uv_key_t;

/* Note: guard clauses should match uv_barrier_init's in src/unix/thread.c. */
#if defined(_AIX) || \
    defined(__OpenBSD__) || \
    !defined(PTHREAD_BARRIER_SERIAL_THREAD)
/* TODO(bnoordhuis) Merge into uv_barrier_t in v2. */
struct _uv_barrier {
  uv_mutex_t mutex;
  uv_cond_t cond;
  unsigned threshold;
  unsigned in;
  unsigned out;
};

typedef struct {
  struct _uv_barrier* b;
# if defined(PTHREAD_BARRIER_SERIAL_THREAD)
  /* TODO(bnoordhuis) Remove padding in v2. */
  char pad[sizeof(pthread_barrier_t) - sizeof(struct _uv_barrier*)];
# endif
} uv_barrier_t;
#else
typedef pthread_barrier_t uv_barrier_t;
#endif

/* Platform-specific definitions for uv_spawn support. */
typedef gid_t uv_gid_t;
typedef uid_t uv_uid_t;

typedef struct dirent uv__dirent_t;

#define UV_DIR_PRIVATE_FIELDS \
  DIR* dir;

#if defined(DT_UNKNOWN)
# define HAVE_DIRENT_TYPES
# if defined(DT_REG)
#  define UV__DT_FILE DT_REG
# else
#  define UV__DT_FILE -1
# endif
# if defined(DT_DIR)
#  define UV__DT_DIR DT_DIR
# else
#  define UV__DT_DIR -2
# endif
# if defined(DT_LNK)
#  define UV__DT_LINK DT_LNK
# else
#  define UV__DT_LINK -3
# endif
# if defined(DT_FIFO)
#  define UV__DT_FIFO DT_FIFO
# else
#  define UV__DT_FIFO -4
# endif
# if defined(DT_SOCK)
#  define UV__DT_SOCKET DT_SOCK
# else
#  define UV__DT_SOCKET -5
# endif
# if defined(DT_CHR)
#  define UV__DT_CHAR DT_CHR
# else
#  define UV__DT_CHAR -6
# endif
# if defined(DT_BLK)
#  define UV__DT_BLOCK DT_BLK
# else
#  define UV__DT_BLOCK -7
# endif
#endif

/* Platform-specific definitions for uv_dlopen support. */
#define UV_DYNAMIC /* empty */

typedef struct {
  void* handle;
  char* errmsg;
} uv_lib_t;

#define UV_LOOP_PRIVATE_FIELDS                                                \
  unsigned long flags;                                                        \
  int backend_fd;                                                             \
  void* pending_queue[2];                                                     \
  // watcher_queue 是 uv__io_t 的观察者队列，其中保存的是 uv__io_t 的结构体
  void* watcher_queue[2];                                                     \
  uv__io_t** watchers;                                                        \
  unsigned int nwatchers;                                                     \
  unsigned int nfds;                                                          \
  // 表述的是work queue，是工作队列
  void* wq[2];                                                                \
  uv_mutex_t wq_mutex;                                                        \
  uv_async_t wq_async;                                                        \
  uv_rwlock_t cloexec_lock;                                                   \
  uv_handle_t* closing_handles;                                               \
  void* process_handles[2];                                                   \
  void* prepare_handles[2];                                                   \
  void* check_handles[2];                                                     \
  void* idle_handles[2];                                                      \
  void* async_handles[2];                                                     \
  void (*async_unused)(void);  /* TODO(bnoordhuis) Remove in libuv v2. */     \
  uv__io_t async_io_watcher;                                                  \
  int async_wfd;                                                              \
  struct {                                                                    \
    void* min;                                                                \
    unsigned int nelts;                                                       \
  } timer_heap;                                                               \
  uint64_t timer_counter;                                                     \
  uint64_t time;                                                              \
  int signal_pipefd[2];                                                       \
  uv__io_t signal_io_watcher;                                                 \
  uv_signal_t child_watcher;                                                  \
  int emfile_fd;                                                              \
  UV_PLATFORM_LOOP_FIELDS                                                     \

#define UV_REQ_TYPE_PRIVATE /* empty */

#define UV_REQ_PRIVATE_FIELDS  /* empty */

#define UV_PRIVATE_REQ_TYPES /* empty */

#define UV_WRITE_PRIVATE_FIELDS                                               \
  void* queue[2];                                                             \
  unsigned int write_index;                                                   \
  uv_buf_t* bufs;                                                             \
  unsigned int nbufs;                                                         \
  int error;                                                                  \
  uv_buf_t bufsml[4];                                                         \

#define UV_CONNECT_PRIVATE_FIELDS                                             \
  void* queue[2];                                                             \

#define UV_SHUTDOWN_PRIVATE_FIELDS /* empty */

#define UV_UDP_SEND_PRIVATE_FIELDS                                            \
  void* queue[2];                                                             \
  struct sockaddr_storage addr;                                               \
  unsigned int nbufs;                                                         \
  uv_buf_t* bufs;                                                             \
  ssize_t status;                                                             \
  uv_udp_send_cb send_cb;                                                     \
  uv_buf_t bufsml[4];                                                         \

#define UV_HANDLE_PRIVATE_FIELDS                                                        \
  uv_handle_t* next_closing;      /* 指向下一个需要关闭的handle */                       \
  unsigned int flags;             /* 状态标记，比如引用、关闭、正在关闭、激活等状态 */     \

#define UV_STREAM_PRIVATE_FIELDS                                              \
  /* 其实 uv_connect_t 是一个请求，从前面的文章我们也知道，在libuv存在 handle 与 request，很明显connect_req就是一个请求，它的作用就是请求建立连接，比如类似建立tcp连接 */
  uv_connect_t *connect_req;                                                  \
  /* uv_shutdown_t也是一个请求，它的作用与uv_connect_t刚好相反，关闭一个连接 */
  uv_shutdown_t *shutdown_req;                                                \
  /* 抽象出来的io观察者 */
  uv__io_t io_watcher;                                                        \
  /* 写数据队列 */
  void* write_queue[2];                                                       \
  /* 完成的写数据队列。 */
  void* write_completed_queue[2];                                             \
  /* 有新连接时的回调函数 */
  uv_connection_cb connection_cb;                                             \
  /* 延时的错误代码 */
  int delayed_error;                                                          \
  /* 接受连接的描述符 fd */
  int accepted_fd;                                                            \
  /* fd 队列，可能有多个fd在排队 */
  void* queued_fds;                                                           \
  /* 目前为空 */
  UV_STREAM_PRIVATE_PLATFORM_FIELDS                                           \

#define UV_TCP_PRIVATE_FIELDS /* empty */

#define UV_UDP_PRIVATE_FIELDS                                                 \
  uv_alloc_cb alloc_cb;                                                       \
  uv_udp_recv_cb recv_cb;                                                     \
  uv__io_t io_watcher;                                                        \
  void* write_queue[2];                                                       \
  void* write_completed_queue[2];                                             \

#define UV_PIPE_PRIVATE_FIELDS                                                \
  const char* pipe_fname; /* strdup'ed */

#define UV_POLL_PRIVATE_FIELDS                                                \
  uv__io_t io_watcher;

#define UV_PREPARE_PRIVATE_FIELDS                                             \
  uv_prepare_cb prepare_cb;                                                   \
  void* queue[2];                                                             \

#define UV_CHECK_PRIVATE_FIELDS                                               \
  uv_check_cb check_cb;                                                       \
  void* queue[2];                                                             \

#define UV_IDLE_PRIVATE_FIELDS                                                \
  uv_idle_cb idle_cb;                                                         \
  void* queue[2];                                                             \

#define UV_ASYNC_PRIVATE_FIELDS                                               \
  uv_async_cb async_cb;                                                       \
  void* queue[2];                                                             \
  int pending;                                                                \

#define UV_TIMER_PRIVATE_FIELDS                                               \
  uv_timer_cb timer_cb;                                                       \
  void* heap_node[3];                                                         \
  uint64_t timeout;                                                           \
  uint64_t repeat;                                                            \
  uint64_t start_id;

#define UV_GETADDRINFO_PRIVATE_FIELDS                                         \
  struct uv__work work_req;                                                   \
  uv_getaddrinfo_cb cb;                                                       \
  struct addrinfo* hints;                                                     \
  char* hostname;                                                             \
  char* service;                                                              \
  struct addrinfo* addrinfo;                                                  \
  int retcode;

#define UV_GETNAMEINFO_PRIVATE_FIELDS                                         \
  struct uv__work work_req;                                                   \
  uv_getnameinfo_cb getnameinfo_cb;                                           \
  struct sockaddr_storage storage;                                            \
  int flags;                                                                  \
  char host[NI_MAXHOST];                                                      \
  char service[NI_MAXSERV];                                                   \
  int retcode;

#define UV_PROCESS_PRIVATE_FIELDS                                             \
  void* queue[2];                                                             \
  int status;                                                                 \

#define UV_FS_PRIVATE_FIELDS                                                  \
  const char *new_path;                                                       \
  uv_file file;                                                               \
  int flags;                                                                  \
  mode_t mode;                                                                \
  unsigned int nbufs;                                                         \
  uv_buf_t* bufs;                                                             \
  off_t off;                                                                  \
  uv_uid_t uid;                                                               \
  uv_gid_t gid;                                                               \
  double atime;                                                               \
  double mtime;                                                               \
  struct uv__work work_req;                                                   \
  uv_buf_t bufsml[4];                                                         \

#define UV_WORK_PRIVATE_FIELDS                                                \
  struct uv__work work_req;

#define UV_TTY_PRIVATE_FIELDS                                                 \
  struct termios orig_termios;                                                \
  int mode;

#define UV_SIGNAL_PRIVATE_FIELDS                                              \
  /* RB_ENTRY(uv_signal_s) tree_entry; */                                     \
  /* 红黑树的节点 */   
  struct {                                                                    \
    struct uv_signal_s* rbe_left;                                             \
    struct uv_signal_s* rbe_right;                                            \
    struct uv_signal_s* rbe_parent;                                           \
    int rbe_color;                                                            \
  } tree_entry;                                                               \
  /* Use two counters here so we don have to fiddle with atomics. */          \
  /* 分别记录了触发信号的次数与处理信号的次数 */
  unsigned int caught_signals;                                                \
  unsigned int dispatched_signals;

#define UV_FS_EVENT_PRIVATE_FIELDS                                            \
  uv_fs_event_cb cb;                                                          \
  UV_PLATFORM_FS_EVENT_FIELDS                                                 \

/* fs open() flags supported on this platform: */
#if defined(O_APPEND)
# define UV_FS_O_APPEND       O_APPEND
#else
# define UV_FS_O_APPEND       0
#endif
#if defined(O_CREAT)
# define UV_FS_O_CREAT        O_CREAT
#else
# define UV_FS_O_CREAT        0
#endif

#if defined(__linux__) && defined(__arm__)
# define UV_FS_O_DIRECT       0x10000
#elif defined(__linux__) && defined(__m68k__)
# define UV_FS_O_DIRECT       0x10000
#elif defined(__linux__) && defined(__mips__)
# define UV_FS_O_DIRECT       0x08000
#elif defined(__linux__) && defined(__powerpc__)
# define UV_FS_O_DIRECT       0x20000
#elif defined(__linux__) && defined(__s390x__)
# define UV_FS_O_DIRECT       0x04000
#elif defined(__linux__) && defined(__x86_64__)
# define UV_FS_O_DIRECT       0x04000
#elif defined(O_DIRECT)
# define UV_FS_O_DIRECT       O_DIRECT
#else
# define UV_FS_O_DIRECT       0
#endif

#if defined(O_DIRECTORY)
# define UV_FS_O_DIRECTORY    O_DIRECTORY
#else
# define UV_FS_O_DIRECTORY    0
#endif
#if defined(O_DSYNC)
# define UV_FS_O_DSYNC        O_DSYNC
#else
# define UV_FS_O_DSYNC        0
#endif
#if defined(O_EXCL)
# define UV_FS_O_EXCL         O_EXCL
#else
# define UV_FS_O_EXCL         0
#endif
#if defined(O_EXLOCK)
# define UV_FS_O_EXLOCK       O_EXLOCK
#else
# define UV_FS_O_EXLOCK       0
#endif
#if defined(O_NOATIME)
# define UV_FS_O_NOATIME      O_NOATIME
#else
# define UV_FS_O_NOATIME      0
#endif
#if defined(O_NOCTTY)
# define UV_FS_O_NOCTTY       O_NOCTTY
#else
# define UV_FS_O_NOCTTY       0
#endif
#if defined(O_NOFOLLOW)
# define UV_FS_O_NOFOLLOW     O_NOFOLLOW
#else
# define UV_FS_O_NOFOLLOW     0
#endif
#if defined(O_NONBLOCK)
# define UV_FS_O_NONBLOCK     O_NONBLOCK
#else
# define UV_FS_O_NONBLOCK     0
#endif
#if defined(O_RDONLY)
# define UV_FS_O_RDONLY       O_RDONLY
#else
# define UV_FS_O_RDONLY       0
#endif
#if defined(O_RDWR)
# define UV_FS_O_RDWR         O_RDWR
#else
# define UV_FS_O_RDWR         0
#endif
#if defined(O_SYMLINK)
# define UV_FS_O_SYMLINK      O_SYMLINK
#else
# define UV_FS_O_SYMLINK      0
#endif
#if defined(O_SYNC)
# define UV_FS_O_SYNC         O_SYNC
#else
# define UV_FS_O_SYNC         0
#endif
#if defined(O_TRUNC)
# define UV_FS_O_TRUNC        O_TRUNC
#else
# define UV_FS_O_TRUNC        0
#endif
#if defined(O_WRONLY)
# define UV_FS_O_WRONLY       O_WRONLY
#else
# define UV_FS_O_WRONLY       0
#endif

/* fs open() flags supported on other platforms: */
#define UV_FS_O_FILEMAP       0
#define UV_FS_O_RANDOM        0
#define UV_FS_O_SHORT_LIVED   0
#define UV_FS_O_SEQUENTIAL    0
#define UV_FS_O_TEMPORARY     0

#endif /* UV_UNIX_H */


struct uv_loop_s {
  void* data;                                             // 用户数据-可以用于任何用途，libuv是不会触碰这个字段的数据的
  unsigned int active_handles;                            // 活跃的 handle 个数
  void* handle_queue[2];                                  // handle队列是一个双向链表，而数组中这两个元素则分别指向next和prev
  union {
    void* unused;                                         // 这是未使用的东西，主要是防止uv_loop_s结构体大小被改变了
    unsigned int count;                                   // 这才是真正使用的东西，用来对在线程池中调用的异步I/O进行计数
  } active_reqs;					                                // request 个数（主要用于文件操作）
  void* internal_fields;
  unsigned int stop_flag;                                 // 事件循环是否结束的标记
  
  unsigned long flags;                                    // libuv 运行的一些标记，目前只有 UV_LOOP_BLOCK_SIGPROF，主要是用于 epoll_wait
							                                            // 的时候屏蔽 SIGPROF 信号，提高性能，SIGPROF 是调操作系统 settimer 函数设置从
							                                            // 而触发的信号                    
  int backend_fd;                                         // epoll 的 fd                     
  void* pending_queue[2];                                 // pending 阶段的队列                    
  void* watcher_queue[2];                                 // watcher_queue 是 uv__io_t 的观察者队列，其中保存的是 uv__io_t 的结构体                    
  uv__io_t** watchers;                                    // watcher_queue 队列的节点中有一个 fd 字段，watchers 以 fd 为索引，记录 fd 所在
							                                            // 的 uv__io_t 结构体                    
  unsigned int nwatchers;                                 // watchers 相关的数量，在 maybe_resize 函数里设置                    
  unsigned int nfds;                                      // watchers 里 fd 个数，一般为 watcher_queue 队列的节点数                    
  void* wq[2];                                            // 线程池的线程处理完任务后把对应的结构体插入到 wq 队列                    
  uv_mutex_t wq_mutex;					                          // 控制 wq 队列互斥访问	
  uv_async_t wq_async;                                    // 用于线程池和主线程通信                    
  uv_rwlock_t cloexec_lock;                               // 用于读写锁的互斥变量                   
  uv_handle_t* closing_handles;                           // closing 阶段的队列。由 uv_close 产生                    
  void* process_handles[2];                               // fork 出来的进程队列                   
  void* prepare_handles[2];                               // libuv 的 prepare 阶段对应的任务队列                   
  void* check_handles[2];                                 // libuv 的 check 阶段对应的任务队列                   
  void* idle_handles[2];                                  // libuv 的 idle 阶段对应的任务队列                    
  void* async_handles[2];                                 // async_handles 队列，在线程池中发送就绪信号给主线程的时候，主线程在 poll io 阶
							                                            // 段执行 uv__async_io 中遍历 async_handles 队列处理里面 pending 为 1 的节点。                    
  void (*async_unused)(void);      			  
  uv__io_t async_io_watcher;                              // 保存了线程通信管道的读端和回调，用于接收线程池的消息，调用 uv__async_io 回调处理 async_handle 队列的节点                    
  int async_wfd;                                          // 用于保存线程池和主线程通信的写端 fd                    
  struct {                                                                    
    void* min;                                                                
    unsigned int nelts;                                                       
  } timer_heap;                                           // 保存定时器二叉堆结构                    
  uint64_t timer_counter;                                 // 管理定时器节点的 id，不断叠加                  
  uint64_t time;                                          // 当前时间，Libuv 会在每次事件循环的开始和 poll io 阶段会更新当前时间，然后在后
							                                            // 续的各个阶段使用，减少对系统调用                   
  int signal_pipefd[2];                                   // 用于 fork 出来的进程和主进程通信的管道，用于非主进程收到信号的时候通知主进程，
							                                            // 然后主进程执行非主进程节点注册的回调                    
  uv__io_t signal_io_watcher;                             
  uv_signal_t child_watcher;				                      // 类似 async_handle，signal_io_watcher 保存了管道读端 fd 和回调，然后注册到 epoll
							                                            // 中，在非主进程收到信号的时候，通过 write 写到管道，最后在 poll io 阶段执行回
							                                            // 调。                    
                                                    
  int emfile_fd;                                          // 备用的 fd                    
};

