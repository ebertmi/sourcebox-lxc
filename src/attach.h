#ifndef SOURCEBOX_ATTACH_H
#define SOURCEBOX_ATTACH_H

#include <vector>

#include "async.h"

class AttachWorker : public LxcWorker {
public:
    AttachWorker(lxc_container *container, NanCallback *callback,
            v8::Local<v8::String> command, v8::Local<v8::Array> args,
            v8::Local<v8::Object> options);

    ~AttachWorker();

    const std::vector<int>& GetParentFds() const;

private:
    void LxcExecute() override;
    void HandleOKCallback() override;

    static int AttachFunction(void *payload);

    int pid;
    std::vector<char*> args;
    std::vector<char*> env;
    std::vector<int> childFds;
    std::vector<int> parentFds;
    std::string cwd = "/";
    bool term;
};

#endif
