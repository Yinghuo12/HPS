#ifndef __SYLAR_HTTP_CONNECTION_H__
#define __SYLAR_HTTP_CONNECTION_H__

#include "sylar/socket_stream.h"
#include "http.h"
#include "sylar/uri.h"
#include "sylar/thread.h"

#include <list>

namespace sylar {
namespace http {

// HTTP 响应结果
struct HttpResult {
    typedef std::shared_ptr<HttpResult> ptr;
    // 错误码
    enum class Error {
        OK = 0,                     // 正常
        INVALID_URL = 1,            // 非法URL
        INVALID_HOST = 2,           // 无法解析HOST
        CONNECT_FAIL = 3,           // 连接失败
        SEND_CLOSE_BY_PEER = 4,     // 连接被对端关闭
        SEND_SOCKET_ERROR = 5,      // 发送请求产生Socket错误
        TIMEOUT = 6,                // 超时
        CREATE_SOCKET_ERROR = 7,    // 创建Socket失败
        POOL_GET_CONNECTION = 8,    // 从连接池中取连接失败
        POOL_INVALID_CONNECTION = 9,// 无效的连接
    };

    HttpResult(int _result, HttpResponse::ptr _response, const std::string& _error)
        :result(_result)
        ,response(_response)
        ,error(_error) {}

    int result;                  // 错误码
    HttpResponse::ptr response;  // HTTP响应结构体
    std::string error;           // 错误描述

    std::string toString() const;
};

// HTTP客户端类
class HttpConnectionPool;
class HttpConnection : public SocketStream {
friend class HttpConnectionPool;
public:
    typedef std::shared_ptr<HttpConnection> ptr;

    static HttpResult::ptr DoGet(const std::string& url, uint64_t timeout_ms, const std::map<std::string, std::string>& headers = {}, const std::string& body = "");
    static HttpResult::ptr DoGet(Uri::ptr uri, uint64_t timeout_ms, const std::map<std::string, std::string>& headers = {}, const std::string& body = "");
    static HttpResult::ptr DoPost(const std::string& url, uint64_t timeout_ms, const std::map<std::string, std::string>& headers = {}, const std::string& body = "");
    static HttpResult::ptr DoPost(Uri::ptr uri, uint64_t timeout_ms, const std::map<std::string, std::string>& headers = {}, const std::string& body = "");

    static HttpResult::ptr DoRequest(HttpMethod method, const std::string& url, uint64_t timeout_ms, const std::map<std::string, std::string>& headers = {}, const std::string& body = "");
    static HttpResult::ptr DoRequest(HttpMethod method, Uri::ptr uri, uint64_t timeout_ms, const std::map<std::string, std::string>& headers = {}, const std::string& body = "");
    static HttpResult::ptr DoRequest(HttpRequest::ptr req, Uri::ptr uri, uint64_t timeout_ms);

    HttpConnection(Socket::ptr sock, bool owner = true);  // owner: 是否掌握所有权
    ~HttpConnection();

    HttpResponse::ptr recvResponse();
    int sendRequest(HttpRequest::ptr req);

private:
    uint64_t m_createTime = 0;      // 创建时间
    uint64_t m_request = 0;         // 请求数
};

class HttpConnectionPool {
public:
    typedef std::shared_ptr<HttpConnectionPool> ptr;
    typedef Mutex MutexType;

    HttpConnectionPool(const std::string& host, const std::string& vhost, uint32_t port, uint32_t max_size, uint32_t max_alive_time, uint32_t max_request);

    HttpConnection::ptr getConnection();
    HttpResult::ptr doGet(const std::string& url, uint64_t timeout_ms, const std::map<std::string, std::string>& headers = {}, const std::string& body = "");
    HttpResult::ptr doGet(Uri::ptr uri, uint64_t timeout_ms, const std::map<std::string, std::string>& headers = {}, const std::string& body = "");
    HttpResult::ptr doPost(const std::string& url, uint64_t timeout_ms, const std::map<std::string, std::string>& headers = {}, const std::string& body = "");
    HttpResult::ptr doPost(Uri::ptr uri, uint64_t timeout_ms, const std::map<std::string, std::string>& headers = {}, const std::string& body = "");
    HttpResult::ptr doRequest(HttpMethod method, const std::string& url, uint64_t timeout_ms, const std::map<std::string, std::string>& headers = {}, const std::string& body = "");
    HttpResult::ptr doRequest(HttpMethod method, Uri::ptr uri, uint64_t timeout_ms, const std::map<std::string, std::string>& headers = {}, const std::string& body = "");
    HttpResult::ptr doRequest(HttpRequest::ptr req, uint64_t timeout_ms);

private:
    static void ReleasePtr(HttpConnection* ptr, HttpConnectionPool* pool);

private:
    std::string m_host;           // 主机地址
    std::string m_vhost;          // 虚拟主机
    uint32_t m_port;              // 端口
    uint32_t m_maxSize;           // 连接池最大连接数
    uint32_t m_maxAliveTime;      // 最大存活时间
    uint32_t m_maxRequest;        // 最大请求数

    MutexType m_mutex;                    // 互斥锁
    std::list<HttpConnection*> m_conns;   // 连接池
    std::atomic<int32_t> m_total = {0};   // 连接池中连接总数, 可能存在被其他线程持有的连接, 因此不一定等于m_conns.size()
};

}
}

#endif