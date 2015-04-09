#ifndef SOURCEBOX_STOP_H
#define SOURCEBOX_STOP_H

#include "async.h"

class StopWorker : public AsyncWorker {
public:
    using AsyncWorker::AsyncWorker;

private:
    void Execute() override;
};

#endif
