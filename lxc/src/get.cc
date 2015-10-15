#include "get.h"

#include "lxc.h"

using namespace v8;

GetWorker::GetWorker(Nan::Callback *callback, const std::string& name,
        const std::string& path, bool requireDefined)
        : AsyncWorker(nullptr, callback), name_(name), path_(path) {
    requireDefined_ = requireDefined;
}

void GetWorker::Execute() {
    container_ = lxc_container_new(name_.c_str(),
            path_.empty() ? nullptr : path_.c_str());

    if (!container_) {
        SetErrorMessage("Failed to create container");
    } else if (requireDefined_ && !container_->is_defined(container_)) {
        SetErrorMessage("Container not found");
        lxc_container_put(container_);
    }
}

void GetWorker::HandleOKCallback() {
    Nan::HandleScope scope;

    Local<Object> wrap = Wrap(container_);

    const int argc = 2;
    Local<Value> argv[argc] = {
        Nan::Null(),
        wrap
    };

    callback->Call(argc, argv);
}
