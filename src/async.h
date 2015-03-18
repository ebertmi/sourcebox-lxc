#ifndef SOURCEBOX_ASYNC_H
#define SOURCEBOX_ASYNC_H

#include <node.h>
#include <nan.h>

#include <lxc/lxccontainer.h>

class AsyncWorker : public NanAsyncWorker {
public:
    AsyncWorker(lxc_container *container, NanCallback *callback)
        : NanAsyncWorker(callback), container(container) { }

protected:
    lxc_container *container;
};

class GetWorker : public AsyncWorker {
public:
    GetWorker(std::string name, std::string path,
            bool errorIfUndefined, NanCallback *callback);

    virtual void Execute();
    virtual void HandleOKCallback();

protected:
    std::string name;
    std::string path;
    bool errorIfUndefined;
};

#endif
