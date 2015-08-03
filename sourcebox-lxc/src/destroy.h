#ifndef SOURCEBOX_DESROY_H
#define SOURCEBOX_DESROY_H

#include "async.h"

class DestroyWorker : public LxcWorker {
public:
    using LxcWorker::LxcWorker;

private:
    void LxcExecute() override;
};

#endif
