
#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <regex>
#include <errno.h>     
#include <mysql/mysql.h>  //mysql

#include "../buffer/buffer.h"
#include "../log/log.h"
#include "../pool/sqlconnpool.h"
#include "../pool/sqlconnRAII.h"

class HttpRequest {
public:
    enum PARSE_STATE {
        REQUEST_LINE, //正在解析首行
        HEADERS,  //头部
        BODY, //体
        FINISH,        //完成
    };

    enum HTTP_CODE {
        NO_REQUEST = 0,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURSE,
        FORBIDDENT_REQUEST, //禁止请求
        FILE_REQUEST, //请求文件
        INTERNAL_ERROR,
        CLOSED_CONNECTION,
    };
    
    HttpRequest() { Init(); }
    ~HttpRequest() = default;

    void Init();
    bool parse(Buffer& buff);

    std::string path() const;
    std::string& path();
    std::string method() const;
    std::string version() const;
    std::string GetPost(const std::string& key) const;
    std::string GetPost(const char* key) const;

    bool IsKeepAlive() const;

    /* 
    todo 
    void HttpConn::ParseFormData() {}
    void HttpConn::ParseJson() {}
    */

private:
    bool ParseRequestLine_(const std::string& line);//解析请求行
    void ParseHeader_(const std::string& line);//解析请求头部
    void ParseBody_(const std::string& line);//解析请求体

    void ParsePath_(); //解析请求资源的路径
    void ParsePost_();
    void ParseFromUrlencoded_(); //解析表单数据

    static bool UserVerify(const std::string& name, const std::string& pwd, bool isLogin);//验证用户登录

    PARSE_STATE state_;//枚举类型，状态
    std::string method_, path_, version_, body_;//请求行内容：请求方法，请求路径，协议版本；  请求体
    std::unordered_map<std::string, std::string> header_;//请求头的内容，存放键和值，
    std::unordered_map<std::string, std::string> post_;//请求报文中的post请求表单数据，主要是用户名和密码

    static const std::unordered_set<std::string> DEFAULT_HTML;//默认的网页
    static const std::unordered_map<std::string, int> DEFAULT_HTML_TAG;//
    static int ConverHex(char ch);//char转换成16进制
};


#endif //HTTP_REQUEST_H