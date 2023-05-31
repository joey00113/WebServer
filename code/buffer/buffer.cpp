#include "buffer.h"

Buffer::Buffer(int initBuffSize) : buffer_(initBuffSize), readPos_(0), writePos_(0) {}

//可以读的字节数，上一次写的位置减去读的位置
size_t Buffer::ReadableBytes() const {
    return writePos_ - readPos_;
}
//可以写的字节数，buffer_大小减去上一次写的位置
size_t Buffer::WritableBytes() const {
    return buffer_.size() - writePos_;
}
//前面可以用的空间，已经读完的数据，其空间可以再利用
size_t Buffer::PrependableBytes() const {
    return readPos_;
}

//开始读的位置
const char* Buffer::Peek() const {
    return BeginPtr_() + readPos_;
}

void Buffer::Retrieve(size_t len) {
    assert(len <= ReadableBytes());
    readPos_ += len;
}

void Buffer::RetrieveUntil(const char* end) {
    assert(Peek() <= end );
    Retrieve(end - Peek());
}

void Buffer::RetrieveAll() {
    bzero(&buffer_[0], buffer_.size());
    readPos_ = 0;
    writePos_ = 0;
}

std::string Buffer::RetrieveAllToStr() {
    std::string str(Peek(), ReadableBytes());
    RetrieveAll();
    return str;
}

const char* Buffer::BeginWriteConst() const {
    return BeginPtr_() + writePos_;
}

//可以开始写的位置
char* Buffer::BeginWrite() {
    return BeginPtr_() + writePos_;
}

//写完后，移动写指针
void Buffer::HasWritten(size_t len) {
    writePos_ += len;
} 

void Buffer::Append(const std::string& str) {
    Append(str.data(), str.length());
}

void Buffer::Append(const void* data, size_t len) {
    assert(data);
    Append(static_cast<const char*>(data), len);
}

//Append(buff, len - writable)，len-writable是buff中存的数据长度
void Buffer::Append(const char* str, size_t len) {
    assert(str);
    //确保有足够的空间可以写数据
    EnsureWriteable(len);
    //已经扩容，writepos为可以开始写的起始位置
    std::copy(str, str + len, BeginWrite());
    HasWritten(len);
}

void Buffer::Append(const Buffer& buff) {
    Append(buff.Peek(), buff.ReadableBytes());
}

void Buffer::EnsureWriteable(size_t len) {
    //容器中可写的长度是否小于len，是则重新创建空间
    if(WritableBytes() < len) {
        MakeSpace_(len);//对vector扩容
    }
    assert(WritableBytes() >= len);
}

ssize_t Buffer::ReadFd(int fd, int* saveErrno) {
    char buff[65535]; //临时的数组，保证能把所有数据读出来
    struct iovec iov[2];//I/O vector，与readv和wirtev操作相关的结构体
    //指针成员iov_base指向一个缓冲区，这个缓冲区是存放的是readv所接收的数据或是writev将要发送的数据。
    //成员iov_len在各种情况下分别确定了接收的最大长度以及实际写入的长度。
    

    /* 分散读， 保证数据全部读完 */
    //buffer_容器满了后，可以读到上面的buff数组
    //第一块缓冲区是buffer_容器，BeginPtr_就是vector的begin()
    iov[0].iov_base = BeginPtr_() + writePos_;
    const size_t writable = WritableBytes();//计算buffer_容器当前剩下的可写长度
    iov[0].iov_len = writable;

    //第二块缓冲区是临时char数组
    iov[1].iov_base = buff;
    iov[1].iov_len = sizeof(buff);

    const ssize_t len = readv(fd, iov, 2);//readv用于在一次函数调用中将数据读到多个非连续缓冲区，返回读出字节数
    if(len < 0){
        *saveErrno = errno;
    }
    //读取的数据小于可写长度，buffer_容器装得下
    else if(static_cast<size_t>(len) <= writable){
        writePos_ += len;
    }
    //读取的数据大于buffer_容器可写长度，需要用到临时数组buff，增加容器大小
    else{
        writePos_ = buffer_.size();
        Append(buff, len - writable); //将已经读到buff的那一部分数据添加到容器尾部
    }
    return len;
}


ssize_t Buffer::WriteFd(int fd, int* saveErrno) {
    size_t readSize = ReadableBytes();
    ssize_t len = write(fd, Peek(), readSize);
    if(len < 0) {
        *saveErrno = errno;
        return len;
    } 
    readPos_ += len;
    return len;
}

char* Buffer::BeginPtr_() {
    return &*buffer_.begin();
}

const char* Buffer::BeginPtr_() const {
    return &*buffer_.begin();
}

//对buffer_容器进行扩容
void Buffer::MakeSpace_(size_t len) {
    //可写长度+前面已读长度小于len，不够装，vector通过resize扩容
    if(WritableBytes() + PrependableBytes() < len) {
        buffer_.resize(writePos_ + len + 1);
    } 
    //前面的已读长度够装
    else {
        //计算待读的数据大小，即往前挪的数据大小
        size_t readable = ReadableBytes();

        //copy(first=待复制元素的起始，last=待复制元素末尾的下一个位置，result=复制结果的起始)
        //把未读的数据往前挪，容器尾部腾出空间
        std::copy(BeginPtr_() + readPos_, BeginPtr_() + writePos_, BeginPtr_());
        readPos_ = 0;//移动后，读位置变为0
        writePos_ = readPos_ + readable;//写位置为待读数据大小
        assert(readable == ReadableBytes());
    }
}