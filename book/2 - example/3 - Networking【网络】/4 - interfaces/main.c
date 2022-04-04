#include <stdio.h>
#include <uv.h>


/*
    Data type for interface addresses.
    typedef struct uv_interface_address_s {
        char* name;
        char phys_addr[6];
        int is_internal;
        union {
            struct sockaddr_in address4;
            struct sockaddr_in6 address6;
        } address;
        union {
            struct sockaddr_in netmask4;
            struct sockaddr_in6 netmask6;
        } netmask;
    } uv_interface_address_t;

    int uv_interface_addresses(uv_interface_address_t** addresses, int* count)
    Gets address information about the network interfaces on the system. 
    An array of count elements is allocated and returned in addresses
    It must be freed by the user, calling uv_free_interface_addresses().

    可以调用uv_interface_addresses获得系统的网络接口信息。下面这个简单的例子打印出
    所有可以获取的信息。这在服务器开始准备绑定IP地址的时候很有用。
*/
int main() {
    char buf[512];
    uv_interface_address_t *info;
    int count, i;

    uv_interface_addresses(&info, &count);
    i = count;

    printf("Number of interfaces: %d\n", count);
    while (i--) {
        uv_interface_address_t interface = info[i];

        printf("Name: %s\n", interface.name);
        printf("Internal? %s\n", interface.is_internal ? "Yes" : "No");
        
        if (interface.address.address4.sin_family == AF_INET) {
            uv_ip4_name(&interface.address.address4, buf, sizeof(buf));
            printf("IPv4 address: %s\n", buf);
        }
        else if (interface.address.address4.sin_family == AF_INET6) {
            uv_ip6_name(&interface.address.address6, buf, sizeof(buf));
            printf("IPv6 address: %s\n", buf);
        }

        printf("\n");
    }

    uv_free_interface_addresses(info, count);
    return 0;
}
