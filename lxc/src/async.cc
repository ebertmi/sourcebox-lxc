#include "async.h"

using namespace v8;

AsyncWorker::AsyncWorker(lxc_container *container, Nan::Callback *callback)
    : Nan::AsyncWorker(callback), container_(container) { }

void LxcWorker::Execute() {
    if (!lxc_container_get(container_)) {
        SetErrorMessage("Invalid container reference");
        return;
    }

    if (!container_->may_control(container_)) {
        SetErrorMessage("Insufficient privileges to control container");
    } else if (requireDefined_ && !container_->is_defined(container_)) {
        SetErrorMessage("Container is not defined");
    } else {
        LxcExecute();
    }

    lxc_container_put(container_);
}
