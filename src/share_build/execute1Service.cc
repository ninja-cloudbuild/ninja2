#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <string>
#include <grpcpp/grpcpp.h>
#include "execute1.grpc.pb.h"
#include "execute1Service.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using execute1::ExecuteRequest;
using execute1::ExecuteResult;
using execute1::ExecuteService;

Execute1Client::Execute1Client(std::shared_ptr<Channel> channel) : stub_(ExecuteService::NewStub(channel)) {}

std::string Execute1Client::Execute1(const std::string& cmd, const std::string& id, const std::string& ninja_host, const std::string& ninja_dir) {
    // char address[INET_ADDRSTRLEN];
    // get_ipv4_address(address, INET_ADDRSTRLEN);

    // Data we are sending to the server.
    ExecuteRequest request;
    request.set_content(cmd);
    request.set_id(id);
    request.set_ninja_host(ninja_host);
    request.set_ninja_dir(ninja_dir);
    request.set_onlylocal(false);

    // Container for the data we expect from the server.
    ExecuteResult reply;

    // 客户端的上下文。它可以用来向服务器传达额外的信息或调整某些RPC行为。
    ClientContext context;

    // The actual RPC.
    Status status = stub_->Execute(&context, request, &reply);

    // Act upon its status.
    if (status.ok()) {
        std::string result = " retcode:" + reply.success() +
                            ",id:" + reply.id() +
                            ",stdOut:" + reply.std_out() +
                            ",stdErr:" + reply.std_err();
        std::cout << result << std::endl;
        return std::string(result);
    } else {
        std::cout << status.error_code() << ": " << status.error_message() << std::endl;
        return "RPC failed";
    }
}

// void get_ipv4_address(char* address, size_t address_size) {
//     struct ifaddrs* ifaddr;
//     struct ifaddrs* ifa;
//     struct sockaddr_in* addr;

//     // 获取本地网络接口地址信息列表
//     if (getifaddrs(&ifaddr) == -1) {
//         perror("getifaddrs");
//         exit(EXIT_FAILURE);
//     }

//     // 遍历网络接口地址信息列表，查找IPv4地址
//     for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
//         if (ifa->ifa_addr == NULL) {
//             continue;
//         }

//         // 判断是否为IPv4地址
//         if (ifa->ifa_addr->sa_family == AF_INET) {
//             addr = (struct sockaddr_in*)ifa->ifa_addr;
//             // 排除回环地址
//             if (!strcmp(inet_ntoa(addr->sin_addr), "127.0.0.1")) {
//                 continue;
//             }
//             // 将IPv4地址拷贝到目标缓冲区
//             strncpy(address, inet_ntoa(addr->sin_addr), address_size - 1);
//             address[address_size - 1] = '\0';
//             break;
//         }
//     }

//     // 释放网络接口地址信息列表
//     freeifaddrs(ifaddr);
// }

// std::string getwd() {
// 	char buffer[1024];
//     if (getcwd(buffer, sizeof(buffer)) != nullptr) {
//         return std::string(buffer);
//     } else {
//         return std::strerror(errno);
//     }
// } 