#ifndef SOURCEBOX_CONFIG_H
#define SOURCEBOX_CONFIG_H

#include "async.h"

class ConfigWorker : public LxcWorker {
public:
    ConfigWorker(lxc_container *container, Nan::Callback *callback,
            const std::string& file, bool save);

private:
    void LxcExecute() override;

    std::string file_;
    bool save_;
};

#endif
