#ifndef SOURCEBOX_CLONE_H
#define SOURCEBOX_CLONE_H

#include "async.h"

class CloneWorker : public LxcWorker {
public:
    CloneWorker(lxc_container *container, Nan::Callback *callback,
            v8::Local<v8::String> name, v8::Local<v8::Object> options);

private:
    void LxcExecute() override;
    void HandleOKCallback() override;

    lxc_container *clone_;

    std::string name_;
    std::string path_;
    std::string bdevtype_;
    uint64_t size_ = 0;
    int flags_ = 0;
};

#endif
