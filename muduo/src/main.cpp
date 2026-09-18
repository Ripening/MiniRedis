#include "EventLoop.h"
#include "TcpServer.h"
#include "InetAddress.h"
#include <iostream>

int main()
{
    EventLoop loop;
    InetAddress listenAddr(8888);

    TcpServer server(&loop, "EchoServer", listenAddr,
                     TcpServer::kReusePort);

    server.setNumThreads(3);

    server.setConnectCallBack([](const TcpConnectionPtr &conn)
                              {
        if (conn->connected())
        {
            std::cout << "new connection: " << conn->getPeerAddress().toIpPort()
                      << " -> " << conn->getLocalAddress().toIpPort() << std::endl;
        }
        else
        {
            std::cout << "connection closed: " << conn->getPeerAddress().toIpPort()
                      << std::endl;
        } });

    server.setMessageCallBack([](const TcpConnectionPtr &conn, Buffer *buffer)
                              {
        std::string msg = buffer->retrieveAllAsString();
        std::cout << "recv from " << conn->getPeerAddress().toIpPort()
                  << ": " << msg.size() << " bytes" << std::endl;
        conn->send(msg); });

    server.setWriteCompleteCallBack([](const TcpConnectionPtr &conn)
                                    {
        std::cout << "write complete: " << conn->getPeerAddress().toIpPort() << std::endl; });

    std::cout << "EchoServer listening on 0.0.0.0:8888" << std::endl;
    server.start();
    loop.loop();

    return 0;
}
