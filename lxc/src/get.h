#ifndef SOURCEBOX_GET_H
#define SOURCEBOX_GET_H

#include "async.h"

class GetWorker : public AsyncWorker {
public:
    GetWorker(Nan::Callback *callback, const std::string& name,
            const std::string& path, bool requireDefined);

private:
    void Execute() override;
    void HandleOKCallback() override;

    std::string name_;
    std::string path_;
};

#endif
