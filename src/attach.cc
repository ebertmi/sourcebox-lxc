#include "attach.h"

#include <unistd.h>
#include <pty.h>
#include <utmp.h>

using namespace v8;

static inline int SetFdFlags(int fd, int flags) {
    int oldFlags = fcntl(fd, F_GETFD);
    if (oldFlags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFD, oldFlags | flags);
}

static inline int SetFlFlags(int fd, int flags) {
    int oldFlags = fcntl(fd, F_GETFL);
    if (oldFlags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFL, oldFlags | flags);
}

AttachWorker::AttachWorker(lxc_container *container, NanCallback *callback,
        Local<String> command, Local<Array> arguments,
        Local<Object> options) : LxcWorker(container, callback) {
    NanScope();

    Local<Value> envValue = options->Get(NanNew("env"));
    Local<Value> cwdValue = options->Get(NanNew("cwd"));
    Local<Value> fdValue = options->Get(NanNew("fds"));

    if (!envValue->IsArray() || !cwdValue->IsString() || !fdValue->IsUint32()) {
        NanThrowTypeError("Invalid argument");
    }

    // command & args
    args.resize(arguments->Length() + 2, nullptr);
    args.front() = strdup(*String::Utf8Value(command));

    for (unsigned int i = 0; i < args.size() - 2; i++) {
        args[i + 1] = strdup(*String::Utf8Value(arguments->Get(i)));
    }

    // env
    Local<Array> envPairs = envValue.As<Array>();

    env.resize(envPairs->Length() + 1, nullptr);

    for (unsigned int i = 0; i < env.size() - 1; i++) {
        env[i] = strdup(*String::Utf8Value(envPairs->Get(i)));
    }

    // cwd
    cwd = *String::Utf8Value(options->Get(NanNew("cwd")));

    // stdio
    term = options->Get(NanNew("term"))->BooleanValue();
    int fdCount = fdValue->Uint32Value() + 3;

    childFds.resize(fdCount);
    parentFds.resize(fdCount);

    int fdPos = 0; // TODO iterator?

    if (term) {
        int master, slave;
        uv_loop_t *loop = uv_default_loop();

        // Acquire read lock to prevent the file descriptor from leaking to
        // other processes before the FD_CLOEXEC flag is set.
        uv_rwlock_rdlock(&loop->cloexec_lock);

        openpty(&master, &slave, nullptr, nullptr, nullptr);
        SetFdFlags(master, FD_CLOEXEC);
        SetFdFlags(slave, FD_CLOEXEC);

        uv_rwlock_rdunlock(&loop->cloexec_lock);

        SetFlFlags(master, O_NONBLOCK);

        for (/* reusing fdPos */; fdPos < 3; fdPos++) {
            parentFds[fdPos] = master;
            childFds[fdPos] = slave;
        }
    }

    for (/* reusing fdPos */; fdPos < fdCount; fdPos++) {
        int fds[2];
        socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
        SetFlFlags(fds[0], O_NONBLOCK);
        parentFds[fdPos] = fds[0];
        childFds[fdPos] = fds[1];
    }
}

AttachWorker::~AttachWorker() {
    // command & args
    for (char *p: args) {
        free(p);
    }

    // env
    for (char *p: env) {
        free(p);
    }

    // stdio
    for (int fd: childFds) {
        close(fd);
    }

}

const std::vector<int>& AttachWorker::GetParentFds() const {
    return parentFds;
}

void AttachWorker::LxcExecute() {
    if (!container->is_running(container)) {
        SetErrorMessage("Container is not running");
        return;
    }

    uv_loop_t *loop = uv_default_loop();

    lxc_attach_options_t options = LXC_ATTACH_OPTIONS_DEFAULT;

    options.initial_cwd = const_cast<char*>(cwd.c_str());

    options.env_policy = LXC_ATTACH_CLEAR_ENV;
    options.extra_env_vars = env.data();

    options.stdin_fd = childFds[0];
    options.stdout_fd = childFds[1];
    options.stderr_fd = childFds[2];

    // Acquire write lock to prevent opening new FDs in other threads.
    uv_rwlock_wrlock(&loop->cloexec_lock);
    int ret = container->attach(container, AttachFunction, this, &options, &pid);
    uv_rwlock_wrunlock(&loop->cloexec_lock);

    if (ret == -1) {
        SetErrorMessage("Could not attach to container");
    }
}

void AttachWorker::HandleOKCallback() {
    NanScope();

    const int argc = 2;

    Local<Value> argv[argc] = {
        NanNull(),
        NanNew<Integer>(pid)
    };

    callback->Call(argc, argv);
}

// gets called within the container
int AttachWorker::AttachFunction(void *payload) {
    AttachWorker *worker = static_cast<AttachWorker*>(payload);

    auto& fds = worker->childFds;
    auto& args = worker->args;

    if (worker->term) {
        login_tty(0);
    }

    for (unsigned int i = 3; i < fds.size(); i++) {
        unsigned int fd = fds[i];
        dup2(fd, i);
        if (fd != i) {
            close(fd);
        }
    }

    execvp(args.front(), args.data());

    // if exec fails, print the error to stderr and exit with code 128 (see GNU
    // conventions)
    perror(args.front());

    return 128;
}
