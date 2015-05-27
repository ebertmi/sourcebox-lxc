#include "clone.h"
#include "lxc.h"

using namespace v8;

CloneWorker::CloneWorker(lxc_container *container, NanCallback *callback,
        Local<String> name, Local<Object> options) : LxcWorker(container, callback) {
    NanScope();

    this->name = *String::Utf8Value(name);

    Local<Value> pathValue = options->Get(NanNew("path"));
    if (pathValue->IsString()) {
        this->path = *String::Utf8Value(pathValue);
    }

    Local<Value> bdevtypeValue = options->Get(NanNew("backingstore"));
    if (bdevtypeValue->IsString()) {
        bdevtype = *String::Utf8Value(bdevtypeValue);
    }

    Local<Value> sizeValue = options->Get(NanNew("size"));
    if (sizeValue->IsNumber()) {
        size = sizeValue->Uint32Value();
    }

    if (options->Get(NanNew("snapshot"))->IsTrue()) {
        flags |= LXC_CLONE_SNAPSHOT;
    }

    if (options->Get(NanNew("keepname"))->IsTrue()) {
        flags |= LXC_CLONE_KEEPNAME;
    }

    if (options->Get(NanNew("keepmac"))->IsTrue()) {
        flags |= LXC_CLONE_KEEPMACADDR;
    }
}

void CloneWorker::LxcExecute() {
    if (container->is_running(container)) {
        SetErrorMessage("Container is running");
        return;
    }

    clone = container->clone(container, name.c_str(),
            path.empty() ? nullptr : path.c_str(),
            flags,
            bdevtype.empty() ? nullptr : bdevtype.c_str(),
            nullptr, size, nullptr);

    if (!clone) {
        SetErrorMessage("Failed to clone container");
    }
}

void CloneWorker::HandleOKCallback() {
    NanScope();

    Local<Object> wrap = Wrap(clone);

    const int argc = 2;
    Local<Value> argv[argc] = {
        NanNull(),
        wrap
    };

    callback->Call(argc, argv);
}
