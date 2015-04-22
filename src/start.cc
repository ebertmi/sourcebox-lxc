#include "start.h"

using namespace v8;

StartWorker::StartWorker(lxc_container *container, NanCallback *callback,
        Local<Array> arguments) : LxcWorker(container, callback) {
    NanScope();

    args.resize(arguments->Length() + 1, nullptr);

    for (unsigned int i = 0; i < args.size() - 1; i++) {
        args[i] = strdup(*String::Utf8Value(arguments->Get(i)));
    }
}

StartWorker::~StartWorker() {
    for (char *p: args) {
        free(p);
    }
}

void StartWorker::LxcExecute() {
    if (!container->start(container, lxcInit, args.data())) {
        SetErrorMessage("Failed to start container");
        // TODO does this return false when the container is already running?
    }
}
