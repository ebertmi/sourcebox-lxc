#include "clone.h"
#include "lxc.h"

using namespace v8;

CloneWorker::CloneWorker(lxc_container *container, NanCallback *callback,
        Local<String> name, Local<Object> options) : LxcWorker(container, callback) {
    NanScope();

    name_ = *String::Utf8Value(name);

    Local<Value> path = options->Get(NanNew("path"));
    if (path->IsString()) {
        path_ = *String::Utf8Value(path);
    }

    Local<Value> bdevtype= options->Get(NanNew("backingstore"));
    if (bdevtype->IsString()) {
        bdevtype_ = *String::Utf8Value(bdevtype);
    }

    Local<Value> size = options->Get(NanNew("size"));
    if (size->IsNumber()) {
        size_ = size->Uint32Value();
    }

    if (options->Get(NanNew("snapshot"))->IsTrue()) {
        flags_ |= LXC_CLONE_SNAPSHOT;
    }

    if (options->Get(NanNew("keepname"))->IsTrue()) {
        flags_ |= LXC_CLONE_KEEPNAME;
    }

    if (options->Get(NanNew("keepmac"))->IsTrue()) {
        flags_ |= LXC_CLONE_KEEPMACADDR;
    }
}

void CloneWorker::LxcExecute() {
    if (container_->is_running(container_)) {
        SetErrorMessage("Container is running");
        return;
    }

    clone_ = container_->clone(container_, name_.c_str(),
            path_.empty() ? nullptr : path_.c_str(),
            flags_,
            bdevtype_.empty() ? nullptr : bdevtype_.c_str(),
            nullptr, size_, nullptr);

    if (!clone_) {
        SetErrorMessage("Failed to clone container");
    }
}

void CloneWorker::HandleOKCallback() {
    NanScope();

    Local<Object> wrap = Wrap(clone_);

    const int argc = 2;
    Local<Value> argv[argc] = {
        NanNull(),
        wrap
    };

    callback->Call(argc, argv);
}
