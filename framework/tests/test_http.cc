#include "sylar/http/http.h"
#include "sylar/log.h"

sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();
void test_request() {
    SYLAR_LOG_INFO(g_logger) << "test_request";
    sylar::http::HttpRequest::ptr req(new sylar::http::HttpRequest);
    req->setHeader("host" , "www.baidu.com");
    req->setBody("hello baidu");
    req->dump(std::cout) << std::endl;
}

void test_response() {
    SYLAR_LOG_INFO(g_logger) << "test_response";
    sylar::http::HttpResponse::ptr rsp(new sylar::http::HttpResponse);
    rsp->setHeader("X-X", "baidu");
    rsp->setBody("hello baidu");
    rsp->setStatus((sylar::http::HttpStatus)400);
    rsp->setClose(false);

    rsp->dump(std::cout) << std::endl;
}

int main(int argc, char** argv) {
    test_request();
    std::cout << "--------------------------" << std::endl;
    test_response();
    return 0;
}