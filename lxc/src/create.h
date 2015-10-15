#ifndef SOURCEBOX_CREATE_H
#define SOURCEBOX_CREATE_H

#include <vector>

#include "async.h"

class CreateWorker : public LxcWorker {
public:
    CreateWorker(lxc_container *container, Nan::Callback *callback, const std::string& templateName,
            const std::string bdevtype, const std::vector<std::string>& args);
    ~CreateWorker();

private:
    void LxcExecute() override;

    std::string template_;
    std::string bdevtype_;
    std::vector<char*> args_;
};

#endif
