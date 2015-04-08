#ifndef SOURCEBOX_GET_H
#define SOURCEBOX_GET_H

#include "async.h"

class GetWorker : public AsyncWorker {
public:
    GetWorker(NanCallback *callback, const std::string& name,
            const std::string& path, bool errorIfUndefined);

private:
    void Execute();
    void HandleOKCallback();

    std::string name;
    std::string path;
    bool errorIfUndefined;
};

#endif
