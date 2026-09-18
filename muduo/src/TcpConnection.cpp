#include "TcpConnection.h"
#include "EventLoop.h"
#include "Channel.h"
#include "Socket.h"
#include <functional>
#include <string>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/tcp.h>
#include <sys/sendfile.h>
#include <fcntl.h> // for open
#include <unistd.h> // for close
static EventLoop* CheckLoopNotNull(EventLoop* loop){
    if(loop==nullptr){
        //log
    }
    return loop;
}
TcpConnection::TcpConnection(EventLoop* loop,
                            const std::string name,
                            int fd,const InetAddress& localAddress,
                            const InetAddress& peerAddress)
            :loop_(CheckLoopNotNull(loop))
            ,name_(name)
            ,socket_(new Socket(fd))
            ,channel_(new Channel(loop,fd))
            ,localAddress_(localAddress)
            ,peerAddress_(peerAddress)
            ,reading_(true)
            ,state_(kConnecting)
            ,highWaterMark_(64*1024*1024)
{
    channel_->setReadEvent([this](){
        this->handleRead();
    });
    channel_->setWriteEvent([this](){
        this->handleWrite();
    });
    channel_->setCloseEvent([this](){
        this->handleClose();
    });
    channel_->setErrEvent([this](){
        this->handleError();
    });
    socket_->setKeepAlive(true);
}

TcpConnection::~TcpConnection(){
    //log
}

void TcpConnection::connectEstablished(){
    setState(kConnected);
    channel_->tied(shared_from_this());
    channel_->enableReading();
    connectCallBack_(shared_from_this());
}

void TcpConnection::connectDestroyed(){
    if(state_==kConnected){
        setState(kDisConnected);
        channel_->disableAll();
        connectCallBack_(shared_from_this());
    }
    channel_->remove();
}
void TcpConnection::shutdown(){
    if(state_==kConnected){
        setState(kDisConnecting);
        loop_->runInLoop([this]{
            this->shutDownInLoop();
        });
    }
}
void TcpConnection::shutDownInLoop(){
    if(!channel_->isWriting()){
        socket_->shutdown();
    }
}
void TcpConnection::setTcpNoDelay(bool on){
    socket_->setTcpNoDelay(on);
}

void TcpConnection::send(const std::string& data){
    if(state_==kConnected){
        if(loop_->isInLoopThread()){
            sendInLoop(data.data(),data.size());
        }
        else{
            loop_->runInLoop([this,data](){
                this->sendInLoop(data.data(),data.size());
            });
        }
    }
}
void TcpConnection::send(Buffer* buf){
    if(state_==kConnected){
        if(loop_->isInLoopThread()){
            sendInLoop(buf->peek(), buf->readAbleBytes());
            buf->retrieveAll();
        }
        else{
            std::string msg = buf->retrieveAllAsString();     // 数据立即拷成自给自足的 string
            loop_->runInLoop([this,msg](){
                this->sendInLoop(msg.data(),msg.size());
            });
        }
    }
}
void TcpConnection::sendInLoop(const void* data,size_t len){
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;
    if(state_==kDisConnected){
        //出现错误
    }
    if(!channel_->isWriting()&&outputBuffer_.readAbleBytes()==0){
        nwrote=::write(socket_->fd(),data,len);
        if(nwrote>=0){
            remaining-=nwrote;
            if(remaining==0&&writeCompleteCallBack_){
                loop_->queueInLoop([this,pr=shared_from_this()](){
                    this->writeCompleteCallBack_(pr);
                });
            }
        }
        else{
            nwrote = 0;
            if (errno != EWOULDBLOCK) // EWOULDBLOCK表示非阻塞情况下没有数据后的正常返回 等同于EAGAIN
            {
                if (errno == EPIPE || errno == ECONNRESET) // SIGPIPE RESET
                {
                    faultError = true;
                }
            }
        }
    }
    if(!faultError&&remaining>0){
        size_t oldLen = outputBuffer_.readAbleBytes();
        if(oldLen+remaining>=highWaterMark_&&oldLen<highWaterMark_&&highWaterMarkCallBack_){
            loop_->queueInLoop([this,pr=shared_from_this(),remaining,oldLen](){
                this->highWaterMarkCallBack_(pr,remaining+oldLen);
            });
        }
        outputBuffer_.append((char *)data + nwrote, remaining);
        if (!channel_->isWriting())
        {
            channel_->enableWriting(); // 这里一定要注册channel的写事件 否则poller不会给channel通知epollout
        }
    }
}

void TcpConnection::handleRead(){
    int saveErrno = 0;
    ssize_t n = inputBuffer_.readFd(socket_->fd(),&saveErrno);
    if(n>0){
        //表示正常运行，根据用户设置的对调函数处理信息
        messageCallBack_(shared_from_this(),&inputBuffer_);
    }else if(n==0){
        //表示客户端已经下线，进行close操作
        handleClose();
    }else{
        //出错了
        errno = saveErrno;
        //log
        handleError();
    }
}

void TcpConnection::handleWriteDone(){
    channel_->disableWriting();
    if(writeCompleteCallBack_){
        loop_->queueInLoop([this,pr=shared_from_this()](){
            this->writeCompleteCallBack_(pr);
        });
    }
    if(state_==kDisConnecting){
        shutDownInLoop();
    }
}

void TcpConnection::handleWrite(){
    int saveError;
    if(channel_->isWriting()){

        if(fileSending_&&sendFileRemaining>0){
            ssize_t sendBytes = ::sendfile(socket_->fd(),sendFileFd_.get(),&sendFileOffset_,sendFileRemaining);
            if(sendBytes>=0){
                sendFileRemaining-=sendBytes;
                if(sendFileRemaining==0){
                    sendFileFd_.reset();    // 发送完毕,归还
                    fileSending_ = false;
                    sendFileOffset_=0;
                    sendFileRemaining=0;
                    handleWriteDone();
                }
            }
            else{
                //log
            }
        }
        else{
            ssize_t n = outputBuffer_.writeFd(socket_->fd(),&saveError);
            if(n>0){
                outputBuffer_.retrieve(n);
                if(outputBuffer_.readAbleBytes()==0){
                    handleWriteDone();
                }
            }else{
                //log 也是出错了
            }
        }
    }
    else{
        //log 出错了
    }
}

void TcpConnection::handleClose(){
    //log 关闭连接
    if(fileSending_){
        sendFileFd_.reset();    // 文件发到一半连接断了,及时释放
        fileSending_ = false;
    }
    setState(kDisConnected);
    channel_->disableAll();
    connectCallBack_(shared_from_this());
    closeCallBack_(shared_from_this());
}

void TcpConnection::handleError()
{
    int optval;
    socklen_t optlen = sizeof optval;
    int err = 0;
    if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    {
        err = errno;
    }
    else
    {
        err = optval;
    }
    //log
}

void TcpConnection::sendFile(const int fileDescriptor, off_t offset, size_t count){
    UniqueFd fd(fileDescriptor);   // 入口即接管:任何提前返回都会自动关闭
    if(!connected()){
        //log
        return;
    }
    if(loop_->isInLoopThread()){
        sendFileInLoop(std::move(fd),offset,count);
    }
    else{
        auto fdPtr = std::make_shared<UniqueFd>(std::move(fd));   // std::function 要求可拷贝,move-only 用 shared_ptr 过桥
        loop_->runInLoop([this,fdPtr,offset,count,pr=shared_from_this()](){
            this->sendFileInLoop(std::move(*fdPtr),offset,count);
        });
    }
}

void TcpConnection::sendFileInLoop(UniqueFd fd, off_t offset, size_t count){
    ssize_t sendBytes = 0;
    size_t remaining = count;

    if(state_!=kConnected){
        //log 连接不在,放弃
        return;                     // fd 析构 → 自动关闭
    }
    if(fileSending_){
        //log 上一个文件未发完,不支持并发
        return;                     // fd 析构 → 自动关闭
    }
    if(!channel_->isWriting()&&outputBuffer_.readAbleBytes()==0){
        sendBytes = ::sendfile(socket_->fd(),fd.get(),&offset,remaining);
        if(sendBytes>=0){
            remaining-=sendBytes;
            if(remaining==0){
                if(writeCompleteCallBack_){
                    loop_->queueInLoop([this, pr=shared_from_this()](){
                        this->writeCompleteCallBack_(shared_from_this());
                    });
                }
                return;             // 发完,fd 析构 → 自动关闭
            }
        }
        else{
            if(errno!=EWOULDBLOCK){
                //log
            }
            if(errno==EPIPE||errno==ECONNRESET){
                return;             // 连接已坏,fd 析构 → 自动关闭
            }
        }
    }


    if(remaining>0){
        this->sendFileFd_ = std::move(fd);    // 成员接管,跨 epoll 周期存活
        this->fileSending_ = true;
        this->sendFileOffset_= offset;
        this->sendFileRemaining = remaining;
        if(!channel_->isWriting()) channel_->enableWriting();
    }
    
}
     