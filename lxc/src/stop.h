#ifndef SOURCEBOX_STOP_H
#define SOURCEBOX_STOP_H

#include "async.h"

class StopWorker : public LxcWorker {
public:
    using LxcWorker::LxcWorker;

private:
    void LxcExecute() override;
};

#endif
