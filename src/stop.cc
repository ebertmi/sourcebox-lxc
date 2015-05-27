#include "stop.h"

using namespace v8;

void StopWorker::LxcExecute() {
    if (!container_->stop(container_)) {
        SetErrorMessage("Failed to stop container");
    }
}
