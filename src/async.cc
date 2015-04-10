#include "async.h"

using namespace v8;

AsyncWorker::AsyncWorker(lxc_container *container, NanCallback *callback)
    : NanAsyncWorker(callback), container(container) { }

void LxcWorker::Execute() {
    if (!lxc_container_get(container)) {
        SetErrorMessage("Invalid container reference");
        return;
    }

    if (!container->may_control(container)) {
        SetErrorMessage("Insufficent privileges to control container");
    } else if (!container->is_defined(container)) {
        SetErrorMessage("Container is not defined");
    } else {
        LxcExecute();
    }

    lxc_container_put(container);
}
