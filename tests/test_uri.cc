#include "sylar/uri.h"
#include <iostream>

int main(int argc, char** argv) {
    // sylar::Uri::ptr uri = sylar::Uri::Create("http://www.baidu.com/test/uri?id=100&name=yinghuo#frg");   // 没有端口号，则默认端口号为80
    // sylar::Uri::ptr uri = sylar::Uri::Create("http://admin@www.yinghuo.website/test/中文/uri?id=100&name=sylar&vv=中文#frg中文");  // 测试解析中文
    sylar::Uri::ptr uri = sylar::Uri::Create("http://admin@www.baidu.com");
    //sylar::Uri::ptr uri = sylar::Uri::Create("http://www.sylar.top/test/uri");
    std::cout << uri->toString() << std::endl;
    auto addr = uri->createAddress();
    std::cout << *addr << std::endl;
    return 0;
}