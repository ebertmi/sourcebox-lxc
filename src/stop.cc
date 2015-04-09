#include "stop.h"

using namespace v8;

void StopWorker::Execute() {
    if (!container->stop(container)) {
        SetErrorMessage("Failed to stop container");
    }
}
