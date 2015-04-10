#include "destroy.h"

using namespace v8;

void DestroyWorker::LxcExecute() {
    if (container->is_running(container)) {
        SetErrorMessage("Container is running");
    } else if (!container->destroy(container)) {
        SetErrorMessage("Failed to destroy container");
    }
}
