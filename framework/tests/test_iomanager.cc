#include "sylar/sylar.h"
#include "sylar/iomanager.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <sys/epoll.h>

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

int sock = 0;

void test_fiber() {
    SYLAR_LOG_INFO(g_logger) << "test_fiber sock=" << sock;

    //sleep(3);

    //close(sock);
    //sylar::IOManager::GetThis()->cancelAll(sock);

    sock = socket(AF_INET, SOCK_STREAM, 0);   // TCP
    fcntl(sock, F_SETFL, O_NONBLOCK);

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8888);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr.s_addr);

    // 如果连接成功，则直接执行回调
    if(!connect(sock, (const sockaddr*)&addr, sizeof(addr))) {

    } else if(errno == EINPROGRESS) {
        SYLAR_LOG_INFO(g_logger) << "add event errno=" << errno << " " << strerror(errno);
        SYLAR_LOG_INFO(g_logger) << "connected";
        sylar::IOManager::GetThis()->addEvent(sock, sylar::IOManager::READ, [](){
            SYLAR_LOG_INFO(g_logger) << "read callback";
        });
        sylar::IOManager::GetThis()->addEvent(sock, sylar::IOManager::WRITE, [](){
            SYLAR_LOG_INFO(g_logger) << "write callback";
            // close(sock);  // 这里主动关闭不会触发 epoll 事件, 所以需要手动取消事件，再关闭socket
            sylar::IOManager::GetThis()->cancelEvent(sock, sylar::IOManager::READ);   // 取消不需要的事件
            close(sock);    // 手动关闭socket
            
            /* 原因：cancelEvent 已经把 READ 事件从 epoll 里删掉了
                close (sock) 会让内核自动把这个 fd 从 epoll 中移除
                close 本身不会触发 EPOLLIN / EPOLLOUT
            */

        });
    } else {
        SYLAR_LOG_INFO(g_logger) << "else " << errno << " " << strerror(errno);
    }

}

void test1() {
    std::cout << "EPOLLIN=" << EPOLLIN << " EPOLLOUT=" << EPOLLOUT << std::endl;
    sylar::IOManager iom(2, false);
    iom.schedule(&test_fiber);
}


int main(int argc, char** argv) {
    test1();
    return 0;
}