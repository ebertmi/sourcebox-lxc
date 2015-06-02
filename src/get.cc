#include "get.h"

#include "lxc.h"

using namespace v8;

GetWorker::GetWorker(NanCallback *callback, const std::string& name, const std::string& path, bool errorIfUndefined)
    : AsyncWorker(nullptr, callback), name_(name), path_(path), errorIfUndefined_(errorIfUndefined) { }

void GetWorker::Execute() {
    container_ = lxc_container_new(name_.c_str(),
            path_.empty() ? nullptr : path_.c_str());

    if (!container_) {
        SetErrorMessage("Failed to create container");
    } else if (errorIfUndefined_ && !container_->is_defined(container_)) {
        SetErrorMessage("Container not found");
        lxc_container_put(container_);
    }
}

void GetWorker::HandleOKCallback() {
    NanScope();

    Local<Object> wrap = Wrap(container_);

    const int argc = 2;
    Local<Value> argv[argc] = {
        NanNull(),
        wrap
    };

    callback->Call(argc, argv);
}
