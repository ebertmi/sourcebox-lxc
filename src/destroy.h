#ifndef SOURCEBOX_DESROY_H
#define SOURCEBOX_DESROY_H

#include "async.h"

class DestroyWorker : public AsyncWorker {
public:
    using AsyncWorker::AsyncWorker;

private:
    void Execute() override;
};

#endif
