#include "lxc.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <lxc/lxccontainer.h>

#include <string>
#include <iostream>

#include <nan.h>

#include "async.h"
#include "util.h"

using namespace v8;

// Helper, Cleanup etc.

static int attachFunc(void *payload) {
    lxc_attach_command_t *command = static_cast<lxc_attach_command_t*>(payload);

    execvp(command->program, command->argv);

    // if exec fails, exit with code 128 (see GNU conventions)
    return 128;
}

NAN_INLINE lxc_container *unwrap(_NAN_METHOD_ARGS) {
    void *ptr = NanGetInternalFieldPointer(args.Holder(), 0);
    return static_cast<lxc_container*>(ptr);
}

NAN_METHOD(waitPids) {
    NanScope();

    Local<Array> pids = args[0].As<Array>();
    int length = pids->Length();

    for (int i = 0; i < length; i++) {
        int pid = pids->Get(i)->NumberValue();
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

// Constructor

Persistent<Function> constructor;

NAN_METHOD(LXCContainer) {
    NanScope();

    if (args.IsConstructCall()) {
        NanReturnThis();
    }

    NanReturnValue(NanNew(constructor)->NewInstance());
}

// New container

NAN_METHOD(newContainer) {
    NanScope();

    std::string name = *String::Utf8Value(args[0]);
    std::string path = *String::Utf8Value(args[1]);

    NanCallback *callback = new NanCallback(args[2].As<Function>());

    NanAsyncQueueWorker(new GetWorker(name, path, true, callback));

    NanReturnUndefined();
}

// Methods

NAN_METHOD(start) {
    NanScope();
    // TODO async is probably required, even when container is daemonized
    // but for testing this will suffice

    lxc_container *container = unwrap(args);

    Local<Array> arguments = args[0].As<Array>();
    int length = arguments->Length();

    char **array = new char*[length + 1];

    for (int i = 0; i < length; i++) {
        String::Utf8Value utf8string(arguments->Get(i));
        array[i] = new char[utf8string.length() + 1];
        std::strcpy(array[i], *utf8string);
    }

    array[length] = NULL;

    bool ret = container->start(container, false, array);

    for (int i = 0; i < length; i++) {
        delete[] array[i];
    }

    delete[] array;

    NanReturnValue(NanNew<Boolean>(ret));
}

NAN_METHOD(attach) {
    NanScope();

    lxc_container *container = unwrap(args);

    lxc_attach_options_t options = LXC_ATTACH_OPTIONS_DEFAULT;
    lxc_attach_command_t command;

    Local<Array> arguments = args[1].As<Array>();
    int length = arguments->Length();

    command.program = strdup(*String::Utf8Value(args[0]));
    command.argv = new char*[length + 2];

    command.argv[0] = command.program;
    command.argv[length + 1] = NULL;

    for (int i = 0; i < length; i++) {
        command.argv[i + 1] = strdup(*String::Utf8Value(arguments->Get(i)));
    }

    int pid;

    int ret = attachWrap(container, attachFunc, &command, &options, &pid);

    for (int i = 0; i < length + 1; i++) {
        free(command.argv[i]);
    }

    delete[] command.argv;

    Local<Object> result = NanNew<Object>();

    if (ret == 0) {
        result->Set(NanNew("pid"), NanNew(pid));
    } else {
        result->Set(NanNew("error"), NanError("Could not attach to container"));
    }

    NanReturnValue(result);
}

// this is just a test, should probably be asynchronous
NAN_METHOD(state) {
    NanScope();

    lxc_container *c = unwrap(args);
    Local<String> state = NanNew(c->state(c));

    NanReturnValue(state);
}

// Initialization

void init(Handle<Object> exports) {
    NanScope();

    on_exit(exitIfInAttachedProcess, NULL);

    Local<FunctionTemplate>constructorTemplate = NanNew<FunctionTemplate>(LXCContainer);

    constructorTemplate->SetClassName(NanNew("LXCContainer"));
    constructorTemplate->InstanceTemplate()->SetInternalFieldCount(1);

    // Methods
    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "start", start);
    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "state", state);
    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "attach", attach);

    NanAssignPersistent(constructor, constructorTemplate->GetFunction());

    // Exports
    exports->Set(NanNew("getContainer"), NanNew<FunctionTemplate>(newContainer)->GetFunction());
    exports->Set(NanNew("waitPids"), NanNew<FunctionTemplate>(waitPids)->GetFunction());
}

NODE_MODULE(lxc, init);
