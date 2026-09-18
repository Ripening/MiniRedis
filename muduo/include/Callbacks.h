#pragma once

#include <memory>
#include <functional>

class TcpConnection;
class Buffer;

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;


using ConnectCallBack = std::function<void(const TcpConnectionPtr&)>;
using CloseCallBack = std::function<void(const TcpConnectionPtr&)>;
using WriteCompleteCallBack = std::function<void(const TcpConnectionPtr&)>;
using HighWaterMarkCallBack = std::function<void(const TcpConnectionPtr&,size_t)>;
using MessageCallBack = std::function<void(const TcpConnectionPtr&,Buffer*)>;