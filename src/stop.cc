#include "stop.h"

using namespace v8;

void StopWorker::LxcExecute() {
    if (!container->stop(container)) {
        SetErrorMessage("Failed to stop container");
    }
}
