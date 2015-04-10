#ifndef SOURCEBOX_ASYNC_H
#define SOURCEBOX_ASYNC_H

#include <node.h>
#include <nan.h>
#include <lxc/lxccontainer.h>

class AsyncWorker : public NanAsyncWorker {
public:
    AsyncWorker(lxc_container *container, NanCallback *callback);

protected:
    lxc_container *container;
};

/**
 * Does some setup/cleanup work such as managing the container's reference
 * count.
 */
class LxcWorker : public AsyncWorker {
public:
    using AsyncWorker::AsyncWorker;

    void Execute() final override;

protected:
    virtual void LxcExecute() = 0;
};

#endif
