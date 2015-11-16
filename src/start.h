#ifndef SOURCEBOX_START_H
#define SOURCEBOX_START_H

#include <vector>

#include "async.h"

class StartWorker : public LxcWorker {
public:
    StartWorker(lxc_container *container, Nan::Callback *callback,
            v8::Local<v8::Array> args);

    ~StartWorker();

private:
    void LxcExecute() override;

    std::vector<char*> args_;
    bool lxcInit_ = false; // TODO start just freezes when this is true
};

#endif
