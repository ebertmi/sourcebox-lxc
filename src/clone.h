#ifndef SOURCEBOX_CLONE_H
#define SOURCEBOX_CLONE_H

#include "async.h"

class CloneWorker : public LxcWorker {
public:
    CloneWorker(lxc_container *container, NanCallback *callback,
            v8::Local<v8::String> name, v8::Local<v8::Object> options);

private:
    void LxcExecute() override;
    void HandleOKCallback() override;

    lxc_container *clone;

    std::string name;
    std::string path;
    std::string bdevtype;
    uint64_t size = 0;
    int flags = 0;
};

#endif
