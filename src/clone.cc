#include "clone.h"
#include "lxc.h"

using namespace v8;

CloneWorker::CloneWorker(lxc_container *container, Nan::Callback *callback,
        Local<String> name, Local<Object> options) : LxcWorker(container, callback) {
    Nan::HandleScope scope;

    name_ = *String::Utf8Value(name);

    Local<Value> path = options->Get(Nan::New("path").ToLocalChecked());
    if (path->IsString()) {
        path_ = *String::Utf8Value(path);
    }

    Local<Value> bdevtype= options->Get(Nan::New("backingstore").ToLocalChecked());
    if (bdevtype->IsString()) {
        bdevtype_ = *String::Utf8Value(bdevtype);
    }

    Local<Value> size = options->Get(Nan::New("size").ToLocalChecked());
    if (size->IsNumber()) {
        size_ = size->Uint32Value();
    }

    if (options->Get(Nan::New("snapshot").ToLocalChecked())->IsTrue()) {
        flags_ |= LXC_CLONE_SNAPSHOT;
    }

    if (options->Get(Nan::New("keepname").ToLocalChecked())->IsTrue()) {
        flags_ |= LXC_CLONE_KEEPNAME;
    }

    if (options->Get(Nan::New("keepmac").ToLocalChecked())->IsTrue()) {
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
    Nan::HandleScope scope;

    Local<Object> wrap = Wrap(clone_);

    const int argc = 2;
    Local<Value> argv[argc] = {
        Nan::Null(),
        wrap
    };

    callback->Call(argc, argv);
}
