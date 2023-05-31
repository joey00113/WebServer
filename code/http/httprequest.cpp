#include "httprequest.h"
using namespace std;

const unordered_set<string> HttpRequest::DEFAULT_HTML{
            "/index", "/register", "/login",
             "/welcome", "/video", "/picture", };

const unordered_map<string, int> HttpRequest::DEFAULT_HTML_TAG {
            {"/register.html", 0}, {"/login.html", 1},  };
            
//初始化
void HttpRequest::Init() {
    method_ = path_ = version_ = body_ = "";
    state_ = REQUEST_LINE; //首先解析首行
    header_.clear();//map集合清空
    post_.clear();
}

bool HttpRequest::IsKeepAlive() const {
    if(header_.count("Connection") == 1) {
        return header_.find("Connection")->second == "keep-alive" && version_ == "1.1";
    }
    return false;
}

//解析的主体函数
bool HttpRequest::parse(Buffer& buff) {
    const char CRLF[] = "\r\n";//换行位置
    if(buff.ReadableBytes() <= 0) {
        return false;
    }
    while(buff.ReadableBytes() && state_ != FINISH) {
        //查找换行符，返回换行符的起始位置
        const char* lineEnd = search(buff.Peek(), buff.BeginWriteConst(), CRLF, CRLF + 2);
        //去除换行符的请求数据，string对象初始化为[begin,end)之间的字符
        std::string line(buff.Peek(), lineEnd);

        //简单的有限状态机，解析请求行、首部、主体的状态迁移
        switch(state_){
        //解析请求行
        case REQUEST_LINE:
            if(!ParseRequestLine_(line)) {
                return false;
            }
            //解析请求资源路径
            ParsePath_();
            break;    
        //解析头部
        case HEADERS:
            ParseHeader_(line);
            //可读字节数小于等于2，表明已经结束
            if(buff.ReadableBytes() <= 2) {
                state_ = FINISH;
            }
            break;
        //解析体
        case BODY:
            ParseBody_(line);
            break;
        default:
            break;
        }
        //当读指针到达写位置，没有可读数据，已经解析完成
        if(lineEnd == buff.BeginWrite()){ 
            break; 
        }
        //解析完一行，移动读指针readpos
        buff.RetrieveUntil(lineEnd + 2);
    }
    LOG_DEBUG("[%s], [%s], [%s]", method_.c_str(), path_.c_str(), version_.c_str());
    return true;
}

//解析路径
void HttpRequest::ParsePath_() {
    //根目录，访问的资源为index.html
    if(path_ == "/") {
        path_ = "/index.html"; 
    }
    else {
        //拼接其他html
        for(auto &item: DEFAULT_HTML) {
            if(item == path_) {
                path_ += ".html";
                break;
            }
        }
    }
}

//解析请求行：请求方法、要访问的资源、使用的HTTP版本
bool HttpRequest::ParseRequestLine_(const string& line){
    //GET / HTTP/1.1
    //正则表达式
    regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    smatch subMatch;
    if(regex_match(line, subMatch, patten)){//匹配的数据放submatch   
        method_ = subMatch[1]; //得到请求方法
        path_ = subMatch[2]; //得到url字段，即请求资源
        version_ = subMatch[3];  //得到http协议版本
        state_ = HEADERS; //解析完请求行后，状态变为解析头部
        return true;
    }
    LOG_ERROR("RequestLine Error");
    return false;
}

//解析头部
//Host：api.github.com
//Connection：keep-alive
void HttpRequest::ParseHeader_(const string& line) {
    regex patten("^([^:]*): ?(.*)$");
    smatch subMatch;
    if(regex_match(line, subMatch, patten)) {
        header_[subMatch[1]] = subMatch[2]; //把请求头部的头部字段作为键，值作为值放进map，用于响应的时候重写？
    }
    else {
        state_ = BODY;//解析完头部后，状态变为解析体
    }
}

//解析请求体，如果是post，要解析，get不用解析请求体
void HttpRequest::ParseBody_(const string& line) {
    body_ = line;
    ParsePost_();
    state_ = FINISH;
    LOG_DEBUG("Body:%s, len:%d", line.c_str(), line.size());
}


int HttpRequest::ConverHex(char ch) {
    if(ch >= 'A' && ch <= 'F') return ch -'A' + 10;
    if(ch >= 'a' && ch <= 'f') return ch -'a' + 10;
    return ch;
}

//解析表单信息
void HttpRequest::ParsePost_() {
    //只考虑post请求，get请求没有请求体不用解析请求体
    if(method_ == "POST" && header_["Content-Type"] == "application/x-www-form-urlencoded") {
        ParseFromUrlencoded_(); //解析请求体，保存键值对并且进行url解码
        if(DEFAULT_HTML_TAG.count(path_)) { //注册或者登录行为
            int tag = DEFAULT_HTML_TAG.find(path_)->second;
            LOG_DEBUG("Tag:%d", tag);
            if(tag == 0 || tag == 1) { 
                bool isLogin = (tag == 1); //tag为0为注册，tag为1为登录
                //注册或者验证用户名密码，成功跳转到welcome
                if(UserVerify(post_["username"], post_["password"], isLogin)) {
                    path_ = "/welcome.html";
                } 
                else {
                    path_ = "/error.html";
                }
            }
        }
    }   
}

//解析加密信息，即用户名和密码
void HttpRequest::ParseFromUrlencoded_() {
    if(body_.size() == 0) { return; }

    string key, value;
    int num = 0;
    int n = body_.size();
    int i = 0, j = 0;

  //例：title=test&sub%5B%5D=1&sub%5B%5D=2&sub%5B%5D=3
  //url编码：%符号在URL编码中用于表示一个特殊字符的编码值的开始
  //它后面跟着两个十六进制数字，表示特殊字符的编码值。例如，"%20"表示空格字符的编码值。
  //

    for(; i < n; i++) {
        char ch = body_[i];
        switch (ch) {
        case '=':
            key = body_.substr(j, i - j);
            j = i + 1;
            break;
        case '+':
            body_[i] = ' ';
            break;
        case '%':
            //url编码的解码
            num = ConverHex(body_[i + 1]) * 16 + ConverHex(body_[i + 2]);
            body_[i + 2] = num % 10 + '0';
            body_[i + 1] = num / 10 + '0';
            i += 2;
            break;
        case '&':
            value = body_.substr(j, i - j);
            j = i + 1;
            post_[key] = value;
            LOG_DEBUG("%s = %s", key.c_str(), value.c_str());
            break;
        default:
            break;
        }
    }
    assert(j <= i);
    if(post_.count(key) == 0 && j < i) {
        value = body_.substr(j, i - j);
        post_[key] = value;
    }
}

//验证登录
bool HttpRequest::UserVerify(const string &name, const string &pwd, bool isLogin) {
    if(name == "" || pwd == ""){ 
        return false; 
    }

    LOG_INFO("Verify name:%s pwd:%s", name.c_str(), pwd.c_str());
    MYSQL* sql;//获取一个mysql连接
    SqlConnRAII(&sql,  SqlConnPool::Instance());//将sql连接封装在一个RAII类，实现资源与对象的生命期绑定
    //代码bug：用匿名函数会导致立刻调用析构函数，则sqlconnRAII内部的sql_连接会被释放，但z这个释放也只是放到队列中，不影响mysql的访问？
    
    assert(sql);
    
    bool flag = false;
    unsigned int j = 0;
    char order[256] = { 0 };
    MYSQL_FIELD *fields = nullptr;
    MYSQL_RES *res = nullptr;
    
    if(!isLogin){ 
        flag = true; 
    }
    /* 查询用户及密码 */
    //将可变参数 “…” 按照format的格式格式化为字符串，然后再将其拷贝至str中。
    //即生成一条sql语句
    snprintf(order, 256, "SELECT username, password FROM user WHERE username='%s' LIMIT 1", name.c_str());
    LOG_DEBUG("%s", order);
    //未查询到用户名信息，返回非0值，查询到用户信息，返回0
    if(mysql_query(sql, order)) { 
        mysql_free_result(res);
        return false; 
    }
    res = mysql_store_result(sql);//获取结果集
    j = mysql_num_fields(res); //获取查询的列数
    fields = mysql_fetch_fields(res); 
    //找到对应行
    while(MYSQL_ROW row = mysql_fetch_row(res)) { //在结果集中不断获取下一行
        LOG_DEBUG("MYSQL ROW: %s %s", row[0], row[1]);
        string password(row[1]);
        /* 登录行为 且 用户名未被使用*/
        if(isLogin) {
            if(pwd == password) { 
                flag = true; 
            }
            else{
                flag = false;
                LOG_DEBUG("pwd error!");
            }
        } 
        else{ 
            flag = false; 
            LOG_DEBUG("user used!");
        }
    }
    mysql_free_result(res);

    /* 注册行为 且 用户名未被使用*/
    if(!isLogin && flag == true) {
        LOG_DEBUG("regirster!");
        bzero(order, 256);
        snprintf(order, 256,"INSERT INTO user(username, password) VALUES('%s','%s')", name.c_str(), pwd.c_str());
        LOG_DEBUG( "%s", order);
        if(mysql_query(sql, order)) { 
            LOG_DEBUG( "Insert error!");
            flag = false; 
        }
        flag = true;
    }
    SqlConnPool::Instance()->FreeConn(sql);//最后手动释放sql。。。。
    LOG_DEBUG( "UserVerify success!!");
    return flag;
}

std::string HttpRequest::path() const{
    return path_;
}

std::string& HttpRequest::path(){
    return path_;
}

std::string HttpRequest::method() const {
    return method_;
}

std::string HttpRequest::version() const {
    return version_;
}

std::string HttpRequest::GetPost(const std::string& key) const {
    assert(key != "");
    if(post_.count(key) == 1) {
        return post_.find(key)->second;
    }
    return "";
}

std::string HttpRequest::GetPost(const char* key) const {
    assert(key != nullptr);
    if(post_.count(key) == 1) {
        return post_.find(key)->second;
    }
    return "";
}