#include "create.h"

using namespace v8;

CreateWorker::CreateWorker(lxc_container *container, Nan::Callback *callback,
        const std::string& templateName, const std::string bdevtype,
        const std::vector<std::string>& args)
        : LxcWorker(container, callback), template_(templateName), bdevtype_(bdevtype) {
    requireDefined_ = false;

    args_.resize(args.size() + 1);
    args_.back() = nullptr;

    for (unsigned int i = 0;  i < args_.size() - 1; i++) {
        args_[i] = strdup(args[i].c_str());
    }
}

CreateWorker::~CreateWorker() {
    for (char *arg : args_) {
        free(arg);
    }
}

void CreateWorker::LxcExecute() {
    if (container_->is_defined(container_)) {
        return SetErrorMessage("Container already exists");
    }
    // TODO additional checks to provide better error messages

    bool ret = container_->create(container_,
            template_.empty() ? nullptr : template_.c_str(),
            bdevtype_.empty() ? nullptr : bdevtype_.c_str(),
            nullptr, LXC_CREATE_QUIET, args_.data());

    if (!ret) {
        SetErrorMessage("Failed to create container");
    }
}
