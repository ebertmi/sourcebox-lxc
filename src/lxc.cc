#include "lxc.h"

#include <pty.h>
#include <sys/wait.h>

#include <string>
#include <vector>

#include <lxc/lxccontainer.h>
#include <nan.h>

#include "get.h"
#include "attach.h"

using namespace v8;

// Helper, Cleanup etc.

NAN_INLINE lxc_container *Unwrap(_NAN_METHOD_ARGS) {
    void *ptr = NanGetInternalFieldPointer(args.Holder(), 0);
    return static_cast<lxc_container*>(ptr);
}

NAN_METHOD(WaitPids) {
    NanScope();

    Local<Array> pids = args[0].As<Array>();
    int length = pids->Length();

    for (int i = 0; i < length; i++) {
        int pid = pids->Get(i)->IntegerValue();
        int status;

        // FIXME handle waitpid errors
        if (waitpid(pid, &status, WNOHANG) > 0) {
            Local<Object> result = NanNew<Object>();
            result->Set(NanNew("pid"), NanNew(pid));

            if (WIFEXITED(status)) {
                int exitCode = WEXITSTATUS(status);
                result->Set(NanNew("exitCode"), NanNew(exitCode));
            } else if (WIFSIGNALED(status)) {
                const char *signalCode = node::signo_string(WTERMSIG(status));
                result->Set(NanNew("signalCode"), NanNew(signalCode));
            } else {
                // child process got stopped or continued
                // FIXME trace?
                NanReturnNull();
            }

            NanReturnValue(result);
        }
    }

    NanReturnNull();
}

NAN_METHOD(Resize) {
    NanScope();

    int fd = args[0]->IntegerValue();

    winsize ws;
    ws.ws_row = args[1]->Uint32Value();
    ws.ws_col = args[2]->Uint32Value();
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    if (ioctl(fd, TIOCSWINSZ, &ws) < 0) {
        NanThrowError(strerror(errno));
    }

    NanReturnUndefined();
}

// Constructor

Persistent<Function> containerConstructor;

NAN_METHOD(LXCContainer) {
    NanScope();

    if (args.IsConstructCall()) {
        NanReturnThis();
    }

    NanReturnValue(NanNew(containerConstructor)->NewInstance());
}

// Get container

NAN_METHOD(GetContainer) {
    NanScope();

    std::string name = *String::Utf8Value(args[0]);
    std::string path = *String::Utf8Value(args[1]);

    NanCallback *callback = new NanCallback(args[2].As<Function>());

    NanAsyncQueueWorker(new GetWorker(callback, name, path, true));

    NanReturnUndefined();
}

// Methods

NAN_METHOD(Start) {
    NanScope();
    // TODO async is probably required, even when container is daemonized
    // but for testing this will suffice

    lxc_container *container = Unwrap(args);

    Local<Array> arguments = args[0].As<Array>();
    int length = arguments->Length();

    char **array = new char*[length + 1];

    for (int i = 0; i < length; i++) {
        String::Utf8Value utf8string(arguments->Get(i));
        array[i] = new char[utf8string.length() + 1];
        std::strcpy(array[i], *utf8string);
    }

    array[length] = nullptr;

    bool ret = container->start(container, false, array);

    for (int i = 0; i < length; i++) {
        delete[] array[i];
    }

    delete[] array;

    NanReturnValue(NanNew<Boolean>(ret));
}

NAN_METHOD(Attach) {
    NanScope();
    // TODO argument error checking

    lxc_container *container = Unwrap(args);

    Local<String> command = args[0]->ToString();
    Local<Array> arguments = args[1].As<Array>();
    Local<Object> options = args[2]->ToObject();

    NanCallback *callback = new NanCallback(args[3].As<Function>());

    AttachWorker *attachWorker = new AttachWorker(container, callback, command, arguments, options);

    const std::vector<int>& fds = attachWorker->GetParentFds();
    Local<Array> fdArray = NanNew<Array>(fds.size());

    for (unsigned int i = 0; i < fds.size(); i++) {
        fdArray->Set(i, NanNew<Number>(fds[i]));
    }

    NanAsyncQueueWorker(attachWorker);

    NanReturnValue(fdArray);
}

// this is just a test, should probably be asynchronous
// TODO: test how much time this actually takes
NAN_METHOD(State) {
    NanScope();

    lxc_container *c = Unwrap(args);
    Local<String> state = NanNew(c->state(c));

    NanReturnValue(state);
}

// Initialization

void Init(Handle<Object> exports) {
    NanScope();

    on_exit(AttachWorker::ExitIfInAttachedProcess, nullptr);

    Local<FunctionTemplate>constructorTemplate = NanNew<FunctionTemplate>(LXCContainer);

    constructorTemplate->SetClassName(NanNew("LXCContainer"));
    constructorTemplate->InstanceTemplate()->SetInternalFieldCount(1);

    // Methods
    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "start", Start);
    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "state", State);
    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "attach", Attach);

    NanAssignPersistent(containerConstructor, constructorTemplate->GetFunction());

    // Exports
    exports->Set(NanNew("getContainer"), NanNew<FunctionTemplate>(GetContainer)->GetFunction());
    exports->Set(NanNew("waitPids"), NanNew<FunctionTemplate>(WaitPids)->GetFunction());
    exports->Set(NanNew("resize"), NanNew<FunctionTemplate>(Resize)->GetFunction());
}

NODE_MODULE(lxc, Init)
