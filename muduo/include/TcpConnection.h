#pragma once
#include "Callbacks.h"
#include "noncopyable.h"
#include "Buffer.h"
#include "UniqueFd.h"
#include <string>
#include <memory>
#include <atomic>
#include "InetAddress.h"
#include <any>

class EventLoop;
class Channel;
class Socket;

class TcpConnection:public noncopyable, public std::enable_shared_from_this<TcpConnection>
{
public:
    TcpConnection(EventLoop* loop,
        const std::string name,
        int fd,
        const InetAddress& localAddress,
        const InetAddress& peerAddress
    );
    ~TcpConnection();
    EventLoop* getLoop()const{return loop_;}
    const std::string& getName()const{return name_;}
    const InetAddress& getLocalAddress()const{return localAddress_;}
    const InetAddress& getPeerAddress()const{return peerAddress_;}
    bool connected(){return state_==kConnected;}

    void send(const std::string& data);
    void send(Buffer* buf);
    void sendFile(const int fileDescriptor, off_t offset, size_t count);   // 接管 fd 所有权,由本连接负责关闭
    void shutdown();

    void setTcpNoDelay(bool on);
    void setConnectCallBack(const ConnectCallBack& cb){connectCallBack_=cb;}
    void setCloseCallBack(const CloseCallBack& cb){closeCallBack_=cb;}
    void setWriteCompleteCallBack(const WriteCompleteCallBack& cb){writeCompleteCallBack_=cb;}
    void setHighWaterMarkCallBack(const HighWaterMarkCallBack& cb,size_t highWaterMark){
        highWaterMarkCallBack_=cb;
        highWaterMark_=highWaterMark;
    }
    void setMessageCallBack(const MessageCallBack& cb){
        messageCallBack_ = cb;
    }
    // 连接建立
    void connectEstablished();
    // 连接销毁
    void connectDestroyed();

    void setContext(std::any context){
        context_ = std::move(context);
    }
    const std::any& getContext() const{
        return context_;
    }
    std::any* getMutableContext(){
        return &context_;
    }
private:
    enum StateE{
        kConnected, //已经建立连接
        kConnecting, //正在建立连接
        kDisConnected, //已经断开连接
        kDisConnecting //正在断开连接
    };
    void setState(StateE state){state_ = state;}

    void handleRead();
    void handleWrite();
    void handleWriteDone();
    void handleClose();
    void handleError();

    void sendInLoop(const void* data,size_t len);
    void shutDownInLoop();
    void sendFileInLoop(UniqueFd fd, off_t offset, size_t count);
    
    std::atomic_bool fileSending_ = false;   //正在发送文件数据
    UniqueFd sendFileFd_;    //正在发送的文件描述符(析构自动关闭)
    off_t sendFileOffset_ =0;   //起始位置
    size_t sendFileRemaining = 0;   //剩余数据数量

    EventLoop* loop_;
    const std::string name_;
    std::atomic_int state_;
    std::atomic_bool reading_;

    std::unique_ptr<Channel> channel_;
    std::unique_ptr<Socket> socket_;

    const InetAddress localAddress_;
    const InetAddress peerAddress_;

    ConnectCallBack connectCallBack_;
    CloseCallBack closeCallBack_;
    WriteCompleteCallBack writeCompleteCallBack_;
    HighWaterMarkCallBack highWaterMarkCallBack_;
    MessageCallBack messageCallBack_;
    size_t highWaterMark_;

    std::any context_;

    Buffer inputBuffer_;
    Buffer outputBuffer_;
};

