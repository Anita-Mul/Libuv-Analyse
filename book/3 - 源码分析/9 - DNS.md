> 在上节中，我们学会了线程池的执行流程，在这一节中，咱们一起了解 DNS 是如何利用线程池完成解析这种慢IO操作的。

## [DNS](https://libuv-docs-chinese.readthedocs.io/zh/latest/dns.html)
 - Libuv 提供了一个异步 dns 解析的能力。包括通过域名查询 ip 和 ip 查询域名两个功能。 Libuv 对操作系统提供的函数进行了封装，配合线程池和事件循环实现异步的能力
#### 数据类型
-   `uv_getaddrinfo_t`[](https://libuv-docs-chinese.readthedocs.io/zh/latest/dns.html#c.uv_getaddrinfo_t "永久链接至目标")

    getaddrinfo请求类型。

<!---->

-   void `(*uv_getaddrinfo_cb)`( [uv_getaddrinfo_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/dns.html#c.uv_getaddrinfo_t "uv_getaddrinfo_t") *  *req* , int  *status* , struct addrinfo*  *res* ) [](https://libuv-docs-chinese.readthedocs.io/zh/latest/dns.html#c.uv_getaddrinfo_cb "永久链接至目标")

    完成后将使用 getaddrinfo 请求结果调用的回调。如果它被取消，状态将有一个值 `UV_ECANCELED`。

<!---->

-   `uv_getnameinfo_t`[](https://libuv-docs-chinese.readthedocs.io/zh/latest/dns.html#c.uv_getnameinfo_t "永久链接至目标")

    getnameinfo请求类型。

<!---->

-   void `(*uv_getnameinfo_cb)`( [uv_getnameinfo_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/dns.html#c.uv_getnameinfo_t "uv_getnameinfo_t") *  *req* , int  *status* , const char*  *hostname* , const char*  *service* ) [](https://libuv-docs-chinese.readthedocs.io/zh/latest/dns.html#c.uv_getnameinfo_cb "永久链接至目标")

    完成后将使用 getnameinfo 请求结果调用的回调。如果它被取消，状态将有一个值 `UV_ECANCELED`。
    
#### API
 - int `uv_getaddrinfo`([uv_loop_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/loop.html#c.uv_loop_t "uv_loop_t")* *loop*, [uv_getaddrinfo_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/dns.html#c.uv_getaddrinfo_t "uv_getaddrinfo_t")* *req*, [uv_getaddrinfo_cb](https://libuv-docs-chinese.readthedocs.io/zh/latest/dns.html#c.uv_getaddrinfo_cb "uv_getaddrinfo_cb") *getaddrinfo_cb*, const char* *node*, const char* *service*, const struct addrinfo* *hints*)
     > 异步[getaddrinfo(3)](http://linux.die.net/man/3/getaddrinfo)
     >
     > 调用[`uv_freeaddrinfo()`](https://libuv-docs-chinese.readthedocs.io/zh/latest/dns.html#c.uv_freeaddrinfo "uv_freeaddrinfo")以释放 addrinfo 结构
 - void `uv_freeaddrinfo`(struct addrinfo* *ai*)
     > 释放结构 addrinfo
 - int `uv_getnameinfo`([uv_loop_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/loop.html#c.uv_loop_t "uv_loop_t")* *loop*, [uv_getnameinfo_t](https://libuv-docs-chinese.readthedocs.io/zh/latest/dns.html#c.uv_getnameinfo_t "uv_getnameinfo_t")* *req*, [uv_getnameinfo_cb](https://libuv-docs-chinese.readthedocs.io/zh/latest/dns.html#c.uv_getnameinfo_cb "uv_getnameinfo_cb") *getnameinfo_cb*, const struct sockaddr* *addr*, int *flags*)
     > 异步[getnameinfo(3)](http://linux.die.net/man/3/getnameinfo)
 #### example
  - 根据 dns 解析地址，解析后的地址进行 TCP 连接，收发数据
    ```c
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <uv.h>

    uv_loop_t *loop;

    void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
      buf->base = malloc(suggested_size);
      buf->len = suggested_size;
    }

    void on_read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
        if (nread < 0) {
            if (nread != UV_EOF)
                fprintf(stderr, "Read error %s\n", uv_err_name(nread));
            uv_close((uv_handle_t*) client, NULL);
            free(buf->base);
            free(client);
            return;
        }

        char *data = (char*) malloc(sizeof(char) * (nread+1));
        data[nread] = '\0';
        strncpy(data, buf->base, nread);

        fprintf(stderr, "%s", data);
        free(data);
        free(buf->base);
    }

    void on_connect(uv_connect_t *req, int status) {
        if (status < 0) {
            fprintf(stderr, "connect failed error %s\n", uv_err_name(status));
            free(req);
            return;
        }

        uv_read_start((uv_stream_t*) req->handle, alloc_buffer, on_read);
        free(req);
    }

    void on_resolved(uv_getaddrinfo_t *resolver, int status, struct addrinfo *res) {
        if (status < 0) {
            fprintf(stderr, "getaddrinfo callback error %s\n", uv_err_name(status));
            return;
        }

        char addr[17] = {'\0'};
        uv_ip4_name((struct sockaddr_in*) res->ai_addr, addr, 16);
        fprintf(stderr, "%s\n", addr);

        uv_connect_t *connect_req = (uv_connect_t*) malloc(sizeof(uv_connect_t));
        uv_tcp_t *socket = (uv_tcp_t*) malloc(sizeof(uv_tcp_t));

        uv_tcp_init(loop, socket);
        uv_tcp_connect(connect_req, socket, (const struct sockaddr*) res->ai_addr, on_connect);

        // 释放结构 addrinfo
        uv_freeaddrinfo(res);
    }


    /*
        libuv提供了一个异步的DNS解决方案。它提供了自己的getaddrinfo。在回调函数中你可以像使用正常的socket
        操作一样

        根据dns解析地址，将解析后的地址再通过tcp发送
    */
    int main() {
        loop = uv_default_loop();

        struct addrinfo hints;
        hints.ai_family = PF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = 0;

        uv_getaddrinfo_t resolver;

        fprintf(stderr, "irc.freenode.net is... ");
        // 如果uv_getaddrinfo返回非零值，说明设置错误了，因此也不会激发回调函数。在函数返回后，所有的参数将会
        // 被回收和释放
        int r = uv_getaddrinfo(loop, &resolver, on_resolved, "irc.freenode.net", "6667", &hints);

        if (r) {
            fprintf(stderr, "getaddrinfo call error %s\n", uv_err_name(r));
            return 1;
        }

        return uv_run(loop, UV_RUN_DEFAULT);
    }
    ```
## 源码解析
#### uv_getaddrinfo
 - 首先对一个 request 进行初始化，然后根据是否传了回调，决定走异步还是同步的模式。同步的方式比较简单，就是直接阻塞 libuv 事件循环，直到解析完成。如果是异步，则给线程池提交一个慢io的任务。其中工作函 数是 uv__getaddrinfo_work。回调是 uv__getaddrinfo_done
 - 源码
    ```c
    int uv_getaddrinfo(uv_loop_t* loop,
                       uv_getaddrinfo_t* req,           // 上层传进来的 req
                       uv_getaddrinfo_cb cb,            // 解析完后的上层回调
                       const char* hostname,            // 需要解析的名字
                       const char* service,             // 查询的过滤条件：服务名。比如 http smtp，也可以是一个端口
                       const struct addrinfo* hints) {  // 其他查询过滤条件
      char hostname_ascii[256];
      size_t hostname_len;
      size_t service_len;
      size_t hints_len;
      size_t len;
      char* buf;
      long rc;

      if (req == NULL || (hostname == NULL && service == NULL))
        return UV_EINVAL;

      /* FIXME(bnoordhuis) IDNA does not seem to work z/OS,
       * probably because it uses EBCDIC rather than ASCII.
       */
    #ifdef __MVS__
      (void) &hostname_ascii;
    #else
      if (hostname != NULL) {
        rc = uv__idna_toascii(hostname,
                              hostname + strlen(hostname),
                              hostname_ascii,
                              hostname_ascii + sizeof(hostname_ascii));
        if (rc < 0)
          return rc;
        hostname = hostname_ascii;
      }
    #endif

      hostname_len = hostname ? strlen(hostname) + 1 : 0;
      service_len = service ? strlen(service) + 1 : 0;
      hints_len = hints ? sizeof(*hints) : 0;
      buf = uv__malloc(hostname_len + service_len + hints_len);

      if (buf == NULL)
        return UV_ENOMEM;

      uv__req_init(loop, req, UV_GETADDRINFO);
      req->loop = loop;
      // 设置请求的回调
      req->cb = cb;
      req->addrinfo = NULL;
      req->hints = NULL;
      req->service = NULL;
      req->hostname = NULL;
      req->retcode = 0;

      /* order matters, see uv_getaddrinfo_done() */
      len = 0;

      if (hints) {
        req->hints = memcpy(buf + len, hints, sizeof(*hints));
        len += sizeof(*hints);
      }

      if (service) {
        req->service = memcpy(buf + len, service, service_len);
        len += service_len;
      }

      if (hostname)
        req->hostname = memcpy(buf + len, hostname, hostname_len);

      // 传了 cb 是异步
      if (cb) {
        uv__work_submit(loop,
                        &req->work_req,
                        UV__WORK_SLOW_IO,
                        uv__getaddrinfo_work,
                        uv__getaddrinfo_done);
        return 0;
      } else {
        // 阻塞式查询，然后执行回调
        uv__getaddrinfo_work(&req->work_req);
        uv__getaddrinfo_done(&req->work_req, 0);
        return req->retcode;
      }
    }
    ```
#### uv__getaddrinfo_work
 - 调用了操作系统提供的 getaddrinfo 去做解析，可能会导致阻塞
 - 源码
    ```c
    static void uv__getaddrinfo_work(struct uv__work* w) {
      uv_getaddrinfo_t* req;
      int err;

      // 根据结构体的字段获取结构体首地址
      req = container_of(w, uv_getaddrinfo_t, work_req);
      // 阻塞在这儿
      err = getaddrinfo(req->hostname, req->service, req->hints, &req->addrinfo);
      req->retcode = uv__getaddrinfo_translate_error(err);
    }
    ```
 #### uv__getaddrinfo_done
  - dns 解析完执行的函数
  - 源码
    ```c
    static void uv__getaddrinfo_done(struct uv__work* w, int status) {
      uv_getaddrinfo_t* req;

      req = container_of(w, uv_getaddrinfo_t, work_req);
      uv__req_unregister(req->loop, req);

      // 释放初始化时申请的内存
      if (req->hints)
        uv__free(req->hints);
      else if (req->service)
        uv__free(req->service);
      else if (req->hostname)
        uv__free(req->hostname);
      else
        assert(0);

      req->hints = NULL;
      req->service = NULL;
      req->hostname = NULL;

      // 解析请求被用户取消了
      if (status == UV_ECANCELED) {
        assert(req->retcode == 0);
        req->retcode = UV_EAI_CANCELED;
      }

      // 执行上层回调
      if (req->cb)
        req->cb(req, req->retcode, req->addrinfo);
    }
    ```
 
 
 
 
 
 
 
 
 
 