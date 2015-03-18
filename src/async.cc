#include "async.h"

#include "lxc.h"

using namespace v8;

NAN_WEAK_CALLBACK(weakCallback) {
    lxc_container_put(data.GetParameter());
}

GetWorker::GetWorker(std::string name, std::string path, bool errorIfUndefined, NanCallback *callback)
    : AsyncWorker(NULL, callback), name(name), path(path), errorIfUndefined(errorIfUndefined) { }

void GetWorker::Execute() {
    container = lxc_container_new(name.c_str(), path.c_str());

    if (!container) {
        SetErrorMessage("Failed to create container");
    } else if (errorIfUndefined && !container->is_defined(container)) {
        SetErrorMessage("Container not found");
        lxc_container_put(container);
    }
}

void GetWorker::HandleOKCallback() {
    NanScope();

    Local<Object> wrap = NanNew(constructor)->NewInstance();
    NanSetInternalFieldPointer(wrap, 0, container);

    NanMakeWeakPersistent(wrap, container, &weakCallback);

    unsigned int argc = 2;

    Local<Value> argv[argc] = {
        NanNull(),
        wrap
    };

    callback->Call(argc, argv);
}
