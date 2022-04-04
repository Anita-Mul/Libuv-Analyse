## TCP【uv_tcp_t --- TCP句柄】
 - [官方文档](https://libuv-docs-chinese.readthedocs.io/zh/latest/tcp.html)
 - TCP是面向连接的，字节流协议，因此基于libuv的stream实现
#### Server
 - 服务器端的建立流程如下
    > 1. uv_tcp_init建立tcp句柄。
    > 2. uv_tcp_bind绑定。
    > 3. uv_listen建立监听，当有新的连接到来时，激活调用回调函数。
    > 4. uv_accept接收链接。
    > 5. 使用stream处理来和客户端通信。
 - tcp-echo-server/main.c - Accepting the client
#### Client
 - 客户端只要调用 uv_tcp_connect 就可以了
   ```c
   uv_tcp_t* socket = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));
   uv_tcp_init(loop, socket);

   uv_connect_t* connect = (uv_connect_t*)malloc(sizeof(uv_connect_t));

   struct sockaddr_in dest;
   uv_ip4_addr("127.0.0.1", 80, &dest);

   uv_tcp_connect(connect, socket, dest, on_connect);
   ```







## UDP【uv_udp_t --- UDP句柄】
 - [官方文档](https://libuv-docs-chinese.readthedocs.io/zh/latest/udp.html)
 - 数据报协议提供无连接的，不可靠的网络通信



## Querying DNS【DNS utility functions】
 - [官方文档](https://libuv-docs-chinese.readthedocs.io/zh/latest/dns.html#c.uv_getaddrinfo)
 - libuv提供了一个异步的DNS解决方案。它提供了自己的getaddrinfo。在回调函数中你可以像使用正常的socket操作一样
 - dns/main.c


## Network interfaces
 - 可以调用uv_interface_addresses获得系统的网络接口信息