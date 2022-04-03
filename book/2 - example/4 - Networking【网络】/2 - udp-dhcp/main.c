#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uv.h>

uv_loop_t *loop;
uv_udp_t send_socket;
uv_udp_t recv_socket;

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  buf->base = malloc(suggested_size);
  buf->len = suggested_size;
}

void on_read(uv_udp_t *req, ssize_t nread, const uv_buf_t *buf, const struct sockaddr *addr, unsigned flags) {
    if (nread < 0) {
        fprintf(stderr, "Read error %s\n", uv_err_name(nread));
        uv_close((uv_handle_t*) req, NULL);
        free(buf->base);
        return;
    }

    char sender[17] = { 0 };
    uv_ip4_name((const struct sockaddr_in*) addr, sender, 16);
    fprintf(stderr, "Recv from %s\n", sender);

    // ... DHCP specific code
    unsigned int *as_integer = (unsigned int*)buf->base;
    unsigned int ipbin = ntohl(as_integer[4]);
    unsigned char ip[4] = {0};
    int i;
    for (i = 0; i < 4; i++)
        ip[i] = (ipbin >> i*8) & 0xff;
    fprintf(stderr, "Offered IP %d.%d.%d.%d\n", ip[3], ip[2], ip[1], ip[0]);

    // 一次读取一个报文
    free(buf->base);
    uv_udp_recv_stop(req);
}

uv_buf_t make_discover_msg() {
    uv_buf_t buffer;
    alloc_buffer(NULL, 256, &buffer);
    memset(buffer.base, 0, buffer.len);

    // BOOTREQUEST
    buffer.base[0] = 0x1;
    // HTYPE ethernet
    buffer.base[1] = 0x1;
    // HLEN
    buffer.base[2] = 0x6;
    // HOPS
    buffer.base[3] = 0x0;
    // XID 4 bytes
    buffer.base[4] = (unsigned int) random();
    // SECS
    buffer.base[8] = 0x0;
    // FLAGS
    buffer.base[10] = 0x80;
    // CIADDR 12-15 is all zeros
    // YIADDR 16-19 is all zeros
    // SIADDR 20-23 is all zeros
    // GIADDR 24-27 is all zeros
    // CHADDR 28-43 is the MAC address, use your own
    buffer.base[28] = 0xe4;
    buffer.base[29] = 0xce;
    buffer.base[30] = 0x8f;
    buffer.base[31] = 0x13;
    buffer.base[32] = 0xf6;
    buffer.base[33] = 0xd4;
    // SNAME 64 bytes zero
    // FILE 128 bytes zero
    // OPTIONS
    // - magic cookie
    buffer.base[236] = 99;
    buffer.base[237] = 130;
    buffer.base[238] = 83;
    buffer.base[239] = 99;

    // DHCP Message type
    buffer.base[240] = 53;
    buffer.base[241] = 1;
    buffer.base[242] = 1; // DHCPDISCOVER

    // DHCP Parameter request list
    buffer.base[243] = 55;
    buffer.base[244] = 4;
    buffer.base[245] = 1;
    buffer.base[246] = 3;
    buffer.base[247] = 15;
    buffer.base[248] = 6;

    return buffer;
}

void on_send(uv_udp_send_t *req, int status) {
    if (status) {
        fprintf(stderr, "Send error %s\n", uv_strerror(status));
        return;
    }
}

/*
    下面的例子展示了一个从DCHP服务器获取ip的例子。
    note
    你必须以管理员的权限运行udp-dhcp，因为它的端口号低于1024
*/
int main() {
    loop = uv_default_loop();

    /*
        ip地址为0.0.0.0，用来绑定所有的接口。255.255.255.255是一个广播地址，
        这也意味着数据报将往所有的子网接口中发送。端口号为0代表着由操作系统
        随机分配一个端口。

        设置了一个接收的socket，端口号为68，作为DHCP客户端，然后开始从中读取
        数据。它会接收所有来自DHCP服务器的返回数据。我们设置了UV_UDP_REUSEADDR
        标记，用来和其他共享端口的 DHCP客户端和平共处

        接着，我们设置了一个类似的发送socket，然后使用uv_udp_send向DHCP服务器（在67端口）
        发送广播

        设置广播发送是非常必要的，否则你会接收到EACCES错误。和此前一样，如果在读写中出错，
        返回码<0。当发送端都匹配不了的时候，广播

        因为UDP不会建立连接，因此回调函数会接收到关于发送者的额外的信息。

        当没有可读数据后，nread等于0。如果addr是null，它代表了没有可读数据（回调函数不会做任何处理）。
        如果不为null，则说明了从addr中接收到一个空的数据报。如果flag为UV_UDP_PARTIAL，则代表了内存分
        配的空间不够存放接收到的数据了，在这种情形下，操作系统会丢弃存不下的数据。
    */
    uv_udp_init(loop, &recv_socket);
    struct sockaddr_in recv_addr;
    uv_ip4_addr("0.0.0.0", 68, &recv_addr);
    uv_udp_bind(&recv_socket, (const struct sockaddr *)&recv_addr, UV_UDP_REUSEADDR);
    uv_udp_recv_start(&recv_socket, alloc_buffer, on_read);

    uv_udp_init(loop, &send_socket);
    struct sockaddr_in broadcast_addr;
    uv_ip4_addr("0.0.0.0", 0, &broadcast_addr);
    uv_udp_bind(&send_socket, (const struct sockaddr *)&broadcast_addr, 0);
    uv_udp_set_broadcast(&send_socket, 1);

    uv_udp_send_t send_req;
    uv_buf_t discover_msg = make_discover_msg();

    struct sockaddr_in send_addr;
    uv_ip4_addr("255.255.255.255", 67, &send_addr);
    uv_udp_send(&send_req, &send_socket, &discover_msg, 1, (const struct sockaddr *)&send_addr, on_send);

    return uv_run(loop, UV_RUN_DEFAULT);
}
