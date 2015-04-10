#include "stop.h"

using namespace v8;

void StopWorker::LxcExecute() {
    if (!container->is_running(container)) {
        SetErrorMessage("Container is not running");
    } else if (!container->stop(container)) {
        SetErrorMessage("Failed to stop container");
    }
}
