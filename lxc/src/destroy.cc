#include "destroy.h"

using namespace v8;

void DestroyWorker::LxcExecute() {
    if (container_->is_running(container_)) {
        SetErrorMessage("Container is running");
    } else if (!container_->destroy(container_)) {
        SetErrorMessage("Failed to destroy container");
    }
}
