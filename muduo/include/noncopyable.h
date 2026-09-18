#pragma once

class noncopyable
{
public:
    noncopyable(const noncopyable&) = delete;
    noncopyable& operator = (const noncopyable&) = delete;
protected:
    noncopyable(/* args */) = default;
    ~noncopyable() = default;
};

