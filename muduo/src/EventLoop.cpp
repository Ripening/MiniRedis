#include "EventLoop.h"
#include "Channel.h"
#include "Poller.h"
#include "TimerQueue.h"
#include <sys/eventfd.h>
#include <unistd.h>
#include <signal.h>
#include <utility>
__thread EventLoop* t_loopInThisThread = nullptr;

const int kPollTime = 1000;

int createEvenFd(){
    int evenfd = ::eventfd(0,EFD_NONBLOCK|EFD_CLOEXEC);
    if(evenfd<0){
        //log
    }
    return evenfd;
}

// 静态对象,main 之前生效:对已断开的 socket 写入时返回 EPIPE 而不是被 SIGPIPE 杀进程
class IgnoreSigPipe
{
public:
    IgnoreSigPipe(){
        ::signal(SIGPIPE,SIG_IGN);
    }
};
IgnoreSigPipe initObj;

EventLoop::EventLoop()
    :poll_(Poller::newDefaultPoller(this)),
    timerQueue_(new TimerQueue(this)),
    wakeupEventFd_(createEvenFd()),
    wakeupChannel_(new Channel(this,wakeupEventFd_)),
    threadPid_(CurrentThread::tid()),
    looping_(false),
    quit_(false),
    callingPendingFuncors_(false)
{
    if(t_loopInThisThread){
        //log
    }
    else t_loopInThisThread = this;
    wakeupChannel_->setReadEvent([this]()->void{
        handleRead();
    });
    wakeupChannel_->enableReading();
}

EventLoop::~EventLoop(){
    timerQueue_.reset();    // 先销毁:它的 timerfd channel 要从 poller 里摘除
    wakeupChannel_->disableAll();
    wakeupChannel_->remove();
    ::close(wakeupEventFd_);
    t_loopInThisThread = nullptr;
}

void EventLoop::loop(){
    looping_ = true;
    quit_ = false;
    while(!quit_){
        activeChannels_.clear();
        poll_->poll(kPollTime,&activeChannels_);
        for(auto& channel:activeChannels_){
            channel->handleEvent();
        }
        doingPendingFunctor();
    }
    looping_ = false;
}

void EventLoop::quit(){
    quit_ = true;
    if(!isInLoopThread()){
        wakeup(); //如果不是在当前的线程中，则唤醒
    }
}

void EventLoop::runInLoop(Functor cb){
    //如果eventloop绑定当前线程则直接执行
    if(isInLoopThread()){
        cb();
    }
    //否则放到其上层回调队列中等待唤醒
    else{
        queueInLoop(cb);
    }
}

void EventLoop::queueInLoop(Functor cb){
    {
        //把待执行的任务放到等待队列中
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFunctions_.emplace_back(cb);
    }
    //如果不是在loopThread中，则唤醒
    if(!isInLoopThread()||callingPendingFuncors_){
        wakeup();
    }
}

void EventLoop::doingPendingFunctor(){
    std::vector<Functor> Functors;
    callingPendingFuncors_ = true;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Functors.swap(pendingFunctions_);
    }
    for(const Functor& functor:Functors){
        functor();
    }
    callingPendingFuncors_ = false;
}

void EventLoop::handleRead(){
    uint64_t one;
    ssize_t n = read(wakeupEventFd_,&one,sizeof(one));
    if(n<sizeof(one)){
        //log
    }
}

void EventLoop::wakeup(){
    uint64_t one = 1;
    ssize_t n = write(wakeupEventFd_,&one,sizeof(one));
    if(n<sizeof(one)){
        //log
    }
}

void EventLoop::updateChannel(Channel* channel){
    poll_->updateChannel(channel);
}

void EventLoop::removeChannel(Channel* channel){
    poll_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel* channel){
    return poll_->hasChannel(channel);
}

TimerId EventLoop::runAt(Timestamp when,TimerCallback cb){
    return timerQueue_->addTimer(std::move(cb),when,0.0);
}

TimerId EventLoop::runAfter(double delaySeconds,TimerCallback cb){
    return timerQueue_->addTimer(std::move(cb),
                                addSeconds(nowTimestamp(),delaySeconds),0.0);
}

TimerId EventLoop::runEvery(double intervalSeconds,TimerCallback cb){
    return timerQueue_->addTimer(std::move(cb),
                                addSeconds(nowTimestamp(),intervalSeconds),
                                intervalSeconds);
}

void EventLoop::cancel(TimerId timerId){
    timerQueue_->cancel(timerId);
}









