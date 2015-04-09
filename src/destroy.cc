#include "destroy.h"

using namespace v8;

void DestroyWorker::Execute() {
    if (!container->destroy(container)) {
        SetErrorMessage("Failed to destroy container");
    }
}
