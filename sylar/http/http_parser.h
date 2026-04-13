#ifndef __SYLAR_HTTP_PARSER_H__
#define __SYLAR_HTTP_PARSER_H__

#include "http.h"
#include "http11_parser.h"
#include "httpclient_parser.h"

namespace sylar {
namespace http {

// HTTP请求解析类
class HttpRequestParser {
public:
    /// HTTP解析类的智能指针
    typedef std::shared_ptr<HttpRequestParser> ptr;
    HttpRequestParser();

    // 返回实际解析的长度, 并且将已解析的数据移除
    size_t execute(char* data, size_t len);

    int isFinished();
    int hasError(); 

    HttpRequest::ptr getData() const { return m_data;}
    void setError(int v) { m_error = v;}
    uint64_t getContentLength();
    const http_parser& getParser() const { return m_parser;}
public:
    // 返回HttpRequest协议解析的缓存大小
    static uint64_t GetHttpRequestBufferSize();
    // 返回HttpRequest协议的最大消息体大小
    static uint64_t GetHttpRequestMaxBodySize();

private:
    http_parser m_parser;     // http_parser
    HttpRequest::ptr m_data;  // HttpRequest结构
    int m_error;              // 错误码
    // 1000: invalid method
    // 1001: invalid version
    // 1002: invalid field
};

class HttpResponseParser {
public:
    // 智能指针类型
    typedef std::shared_ptr<HttpResponseParser> ptr;
    HttpResponseParser();

    // 返回实际解析的长度,并且移除已解析的数据
    size_t execute(char* data, size_t len, bool chunk);  // 是否在解析chunk

    int isFinished();
    int hasError(); 

    HttpResponse::ptr getData() const { return m_data;}
    void setError(int v) { m_error = v;}
    uint64_t getContentLength();
    const httpclient_parser& getParser() const { return m_parser;}
public:
    static uint64_t GetHttpResponseBufferSize();
    static uint64_t GetHttpResponseMaxBodySize();

private:
    httpclient_parser m_parser; // httpclient_parser
    HttpResponse::ptr m_data;   // HttpResponse
    int m_error;                // 错误码
    // 1001: invalid version
    // 1002: invalid field
};

}
}

#endif