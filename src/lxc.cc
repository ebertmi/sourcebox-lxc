#include "lxc.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <pty.h>
#include <utmp.h>
#include <lxc/lxccontainer.h>

#include <string>

#include <nan.h>

#include "async.h"
#include "util.h"

using namespace v8;

struct AttachPayload {
    char *command;
    char **args;
    bool term;
    int *fds;
    int fdCount;
};

// Helper, Cleanup etc.

static int attachFunc(void *payload) {
    AttachPayload *options = static_cast<AttachPayload*>(payload);

    if (options->term) {
        login_tty(0);
    }

    for (int i = 3; i < options->fdCount; i++) {
        int fd = options->fds[i];
        dup2(fd, i);
        if (fd != i) {
            close(fd);
        }
    }

    execvp(options->command, options->args);

    // if exec fails, exit with code 128 (see GNU conventions)
    return 128;
}

static inline void setFdFlags(int fd, int flags) {
    int oldFlags = fcntl(fd, F_GETFD, 0);
    fcntl(fd, F_SETFD, oldFlags | flags);
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

    Local<Object> options = args[2]->ToObject();
    AttachPayload payload;

    lxc_attach_options_t attachOptions = LXC_ATTACH_OPTIONS_DEFAULT;

    // args
    Local<Array> arguments = args[1].As<Array>();
    int argc = arguments->Length() + 2;

    payload.command = strdup(*String::Utf8Value(args[0]));
    payload.args = new char*[argc];

    payload.args[0] = payload.command;
    payload.args[argc - 1] = NULL;

    for (int i = 0; i < argc - 2; i++) {
        payload.args[i + 1] = strdup(*String::Utf8Value(arguments->Get(i)));
    }

    // env
    //TODO

    // stdio
    payload.term = options->Get(NanNew("term"))->BooleanValue();
    payload.fdCount = options->Get(NanNew("fds"))->Uint32Value() + 3;

    int *parentFds = new int[payload.fdCount];;
    int *childFds = new int[payload.fdCount];
    int fdPos = 0;

    if (payload.term) {
        int master, slave;

        // TODO to be super safe, we should acquire the libuv fork lock for the
        // openpty and cloexec calls
        openpty(&master, &slave, NULL, NULL, NULL);
        setFdFlags(master, FD_CLOEXEC);

        for (/* reusing fdPos */; fdPos < 3; fdPos++) {
            parentFds[fdPos] = master;
            childFds[fdPos] = slave;
        }
    }

    for (/* reusing fdPos */; fdPos < payload.fdCount; fdPos++) {
        int fds[2];
        socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
        parentFds[fdPos] = fds[0];
        childFds[fdPos] = fds[1];
    }

    attachOptions.stdin_fd = childFds[0];
    attachOptions.stdout_fd = childFds[1];
    attachOptions.stderr_fd = childFds[2];
    payload.fds = childFds;

    // attach
    int pid;
    int ret = attachWrap(container, attachFunc, &payload, &attachOptions, &pid);

    // cleanup
    for (int i = 0; i < argc - 1; i++) {
        free(payload.args[i]);
    }
    //TODO env

    delete[] payload.args;

    Local<Object> fdArray = NanNew<Array>();

    for (int i = 0; i < payload.fdCount; i++) {
        close(childFds[i]);
        setFdFlags(parentFds[i], O_NONBLOCK);
        fdArray->Set(i, NanNew<Number>(parentFds[i]));
    }

    delete[] parentFds;
    delete[] childFds;

    Local<Object> result = NanNew<Object>();

    if (ret == 0) {
        result->Set(NanNew("pid"), NanNew(pid));
        result->Set(NanNew("fds"), fdArray);
    } else {
        for (int i = 0; i < payload.fdCount; i++) {
            close(parentFds[i]);
        }
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
