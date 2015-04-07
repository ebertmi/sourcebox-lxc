#ifndef SOURCEBOX_ASYNC_H
#define SOURCEBOX_ASYNC_H

#include <vector>

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

private:
    void Execute();
    void HandleOKCallback();

    std::string name;
    std::string path;
    bool errorIfUndefined;
};

class AttachWorker : public AsyncWorker {
public:
    AttachWorker(lxc_container *container, NanCallback *callback,
            v8::Local<v8::String> command, v8::Local<v8::Array> args,
            v8::Local<v8::Object> options);

    ~AttachWorker();

    const std::vector<int>& getParentFds() const;

    static void exitIfInAttachedProcess(int status, void *);

private:
    void Execute();
    void HandleOKCallback();

    static int attachFunc(void *payload);

    int pid;
    std::vector<char*> args;
    std::vector<char*> env;
    std::vector<int> childFds;
    std::vector<int> parentFds;
    std::string cwd;
    bool term;

    static bool inAttachedProcess;
};

#endif
