#ifndef SOURCEBOX_START_H
#define SOURCEBOX_START_H

#include <vector>

#include "async.h"

class StartWorker : public LxcWorker {
public:
    StartWorker(lxc_container *container, NanCallback *callback,
            v8::Local<v8::Array> args);

    ~StartWorker();

private:
    void LxcExecute() override;

    std::vector<char*> args;
    bool lxcInit = false; // TODO start just freezes when this is true
};

#endif
