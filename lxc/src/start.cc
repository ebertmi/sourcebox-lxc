#include "start.h"

using namespace v8;

StartWorker::StartWorker(lxc_container *container, Nan::Callback *callback,
        Local<Array> arguments) : LxcWorker(container, callback) {
    Nan::HandleScope scope;

    args_.resize(arguments->Length() + 1, nullptr);

    for (unsigned int i = 0; i < args_.size() - 1; i++) {
        args_[i] = strdup(*String::Utf8Value(arguments->Get(i)));
    }
}

StartWorker::~StartWorker() {
    for (char *p: args_) {
        free(p);
    }
}

void StartWorker::LxcExecute() {
    if (!container_->start(container_, lxcInit_, args_.data())) {
        SetErrorMessage("Failed to start container");
    }
}
