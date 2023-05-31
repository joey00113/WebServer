#include "httpresponse.h"

using namespace std;

//文件类型：文件类型描述
const unordered_map<string, string> HttpResponse::SUFFIX_TYPE = {
    { ".html",  "text/html" },
    { ".xml",   "text/xml" },
    { ".xhtml", "application/xhtml+xml" },
    { ".txt",   "text/plain" },
    { ".rtf",   "application/rtf" },
    { ".pdf",   "application/pdf" },
    { ".word",  "application/nsword" },
    { ".png",   "image/png" },
    { ".gif",   "image/gif" },
    { ".jpg",   "image/jpeg" },
    { ".jpeg",  "image/jpeg" },
    { ".au",    "audio/basic" },
    { ".mpeg",  "video/mpeg" },
    { ".mpg",   "video/mpeg" },
    { ".avi",   "video/x-msvideo" },
    { ".gz",    "application/x-gzip" },
    { ".tar",   "application/x-tar" },
    { ".css",   "text/css "},
    { ".js",    "text/javascript "},
};

//状态码：状态描述
const unordered_map<int, string> HttpResponse::CODE_STATUS = {
    { 200, "OK" },
    { 400, "Bad Request" },
    { 403, "Forbidden" },
    { 404, "Not Found" },
};

//错误状态码：显示对应错误的html
const unordered_map<int, string> HttpResponse::CODE_PATH = {
    { 400, "/400.html" },
    { 403, "/403.html" },
    { 404, "/404.html" },
};

//构造函数
HttpResponse::HttpResponse() {
    code_ = -1;
    path_ = srcDir_ = "";
    isKeepAlive_ = false;
    mmFile_ = nullptr; 
    mmFileStat_ = { 0 };
};

//析构函数
HttpResponse::~HttpResponse() {
    UnmapFile(); //解除内存映射
}

//初始化响应对象
void HttpResponse::Init(const string& srcDir, string& path, bool isKeepAlive, int code){
    assert(srcDir != "");
    //内存映射的指针不为空，释放
    if(mmFile_){ 
        UnmapFile(); 
    }
    code_ = code;
    isKeepAlive_ = isKeepAlive;
    path_ = path;
    srcDir_ = srcDir;
    mmFile_ = nullptr; 
    mmFileStat_ = { 0 };
}

//创建一个响应对象，写到写缓冲区
void HttpResponse::MakeResponse(Buffer& buff) {
    /* 判断请求的资源文件 */
    //获取文件资源的状态信息，如果获取失败或者访问的资源是目录，404
    if(stat((srcDir_ + path_).data(), &mmFileStat_) < 0 || S_ISDIR(mmFileStat_.st_mode)) {
        code_ = 404;
    }
    //没有权限，403
    else if(!(mmFileStat_.st_mode & S_IROTH)) {
        code_ = 403;
    }
    //code默认值为-1，那么成功找到资源
    else if(code_ == -1) { 
        code_ = 200; 
    }
    ErrorHtml_();
    AddStateLine_(buff); //往写缓冲区添加响应首行/状态行
    AddHeader_(buff);//往写缓冲区添加响应头部
    AddContent_(buff);//往写缓冲区添加响应正文
}

char* HttpResponse::File() {
    return mmFile_;
}

size_t HttpResponse::FileLen() const {
    return mmFileStat_.st_size;
}

void HttpResponse::ErrorHtml_() {
    if(CODE_PATH.count(code_) == 1) {
        path_ = CODE_PATH.find(code_)->second;
        stat((srcDir_ + path_).data(), &mmFileStat_);
    }
}

//往写缓冲区中添加状态行
void HttpResponse::AddStateLine_(Buffer& buff) {
    string status;
    //状态码code有相应的状态描述，200=OK
    if(CODE_STATUS.count(code_) == 1) {
        //设置状态描述
        status = CODE_STATUS.find(code_)->second;
    }
    else {
        //找不到对应状态，状态描述为404 not found
        code_ = 400;
        status = CODE_STATUS.find(400)->second;
    }
    //响应首行格式：
    //HTTP/1.1 200 OK
    buff.Append("HTTP/1.1 " + to_string(code_) + " " + status + "\r\n");
}

//往写缓冲区中添加响应首部
void HttpResponse::AddHeader_(Buffer& buff) {
    //Connection:keep-alive
    buff.Append("Connection: ");
    if(isKeepAlive_) {
        buff.Append("keep-alive\r\n");
        buff.Append("keep-alive: max=6, timeout=120\r\n");
    } else{
        buff.Append("close\r\n");
    }
    buff.Append("Content-type: " + GetFileType_() + "\r\n");
}

//往写缓冲区中添加响应正文，请求的资源放在响应正文
void HttpResponse::AddContent_(Buffer& buff) {
    //打开资源文件，得到一个文件描述符
    int srcFd = open((srcDir_ + path_).data(), O_RDONLY);
    if(srcFd < 0) { 
        ErrorContent(buff, "File NotFound!");
        return; 
    }
    
    /* 使用mmap将文件映射到内存，提高文件的访问速度 
        MAP_PRIVATE 建立一个写入时拷贝的私有映射*/
    LOG_DEBUG("file path %s", (srcDir_ + path_).data());
    int* mmRet = (int*)mmap(0, mmFileStat_.st_size, PROT_READ, MAP_PRIVATE, srcFd, 0);
    if(*mmRet == -1) {
        ErrorContent(buff, "File NotFound!");
        return; 
    }
    mmFile_ = (char*)mmRet; //映射到内存的文件指针
    close(srcFd);
    //最后的首部
    /* Conten-length：.... \r\n
       \r\n  */   
    //此时，响应报文的请求行和首部在写buffer里面，响应正文在内存映射中
    buff.Append("Content-length: " + to_string(mmFileStat_.st_size) + "\r\n\r\n");
}

//解除内存映射，释放内存映射指针
void HttpResponse::UnmapFile() {
    if(mmFile_) {
        munmap(mmFile_, mmFileStat_.st_size);
        mmFile_ = nullptr;
    }
}

//判断文件类型
string HttpResponse::GetFileType_() {
    //获取后缀，例如.html .jpg
    string::size_type idx = path_.find_last_of('.');
    if(idx == string::npos) {
        return "text/plain";
    }
    string suffix = path_.substr(idx);
    if(SUFFIX_TYPE.count(suffix) == 1) {
        return SUFFIX_TYPE.find(suffix)->second;
    }
    return "text/plain";
}

void HttpResponse::ErrorContent(Buffer& buff, string message) {
    string body;
    string status;
    body += "<html><title>Error</title>";
    body += "<body bgcolor=\"ffffff\">";
    if(CODE_STATUS.count(code_) == 1) {
        status = CODE_STATUS.find(code_)->second;
    } else {
        status = "Bad Request";
    }
    body += to_string(code_) + " : " + status  + "\n";
    body += "<p>" + message + "</p>";
    body += "<hr><em>TinyWebServer</em></body></html>";

    buff.Append("Content-length: " + to_string(body.size()) + "\r\n\r\n");
    buff.Append(body);
}
