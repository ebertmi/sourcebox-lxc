#include "attach.h"

#include <sys/wait.h>
#include <unistd.h>
#include <pty.h>
#include <utmp.h>

#if NAUV_UVVERSION >= 0x000b14
#define UV_CLOEXEC_LOCK
#endif

using namespace v8;

NanCallback *exitCallback = new NanCallback();
static Persistent<Object> attachedProcesses;
static uv_signal_t sigchldHandle;

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

static void MaybeUnref() {
    NanScope();

    Local<Object> processes = NanNew(attachedProcesses);
    Local<Array> pids = processes->GetOwnPropertyNames();
    int length = pids->Length();

    for (int i = 0; i < length; i++) {
        Local<Object> process = processes->Get(pids->Get(i))->ToObject();
        if (process->Get(NanNew("_ref"))->BooleanValue()) {
            // at least one process is still referenced
            return;
        }
    }

    uv_unref(reinterpret_cast<uv_handle_t*>(&sigchldHandle));
}

static void ReapChildren(uv_signal_t* handle, int signal) {
    NanScope();

    Local<Object> processes = NanNew(attachedProcesses);
    Local<Array> pids = processes->GetOwnPropertyNames();
    int length = pids->Length();

    bool reaped = false;

    for (int i = 0; i < length; i++) {
        int pid = pids->Get(i)->Uint32Value();
        int status;
        int ret;

        do {
            ret = waitpid(pid, &status, WNOHANG);
        } while (ret == -1 && errno == EINTR);

        if (ret == 0) {
            continue;
        }

        if (ret == -1) {
            if (errno != ECHILD) {
                abort();
            }
            continue;
        }

        Local<Value> exitCode;
        Local<Value> signalCode;

        if (WIFEXITED(status)) {
            exitCode = NanNew<Uint32>(WEXITSTATUS(status));
            signalCode = NanNull();
        } else if (WIFSIGNALED(status)) {
            signalCode = NanNew(node::signo_string(WTERMSIG(status)));
            exitCode = NanNull();
        } else {
            // child process got stopped or continued
            continue;
        }

        const int argc = 3;
        Local<Value> argv[argc] = {
            processes->Get(pid),
            exitCode,
            signalCode
        };

        processes->Delete(pid);
        exitCallback->Call(argc, argv);
        reaped = true;
    }

    if (reaped) {
        MaybeUnref();
    }
}

AttachWorker::AttachWorker(lxc_container *container, Local<Object> attachedProcess,
        const std::string& command, const std::vector<std::string>& args,
        const std::string& cwd, const std::vector<std::string>& env,
        const std::vector<int>& fds, bool term, int uid, int gid)
        : LxcWorker(container, nullptr), cwd_(cwd), fds_(fds),
        term_(term), uid_(uid), gid_(gid) {
    SaveToPersistent("attachedProcess", attachedProcess);

    // command & args
    args_.resize(args.size() + 2);
    args_.front() = strdup(command.c_str());
    args_.back() = nullptr;

    for (unsigned int i = 0; i < args_.size() - 2; i++) {
        args_[i + 1] = strdup(args[i].c_str());
    }

    // env
    env_.resize(env.size() + 1);
    env_.back() = nullptr;

    for (unsigned int i = 0; i < env_.size() - 1; i++) {
        env_[i] = strdup(env[i].c_str());
    }
}

AttachWorker::~AttachWorker() {
    // command & args
    for (char *p: args_) {
        free(p);
    }

    // env
    for (char *p: env_) {
        free(p);
    }

    // stdio
    for (int fd: fds_) {
        close(fd);
    }
}

void AttachWorker::LxcExecute() {
    if (!container_->is_running(container_)) {
        SetErrorMessage("Container is not running");
        return;
    }

    lxc_attach_options_t options = LXC_ATTACH_OPTIONS_DEFAULT;

    options.initial_cwd = const_cast<char*>(cwd_.c_str());

    options.env_policy = LXC_ATTACH_CLEAR_ENV;
    options.extra_env_vars = env_.data();

    options.uid = uid_;
    options.gid = gid_;

    options.stdin_fd = fds_[0];
    options.stdout_fd = fds_[1];
    options.stderr_fd = fds_[2];

    int errorFds[2];
    socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, errorFds);
    errorFd_ = errorFds[1];

#ifdef UV_CLOEXEC_LOCK
    // Acquire write lock to prevent opening new FDs in other threads.
    uv_loop_t *loop = uv_default_loop();
    uv_rwlock_wrlock(&loop->cloexec_lock);
#endif

    int ret = container_->attach(container_, AttachFunction, this, &options, &pid_);

#ifdef UV_CLOEXEC_LOCK
    uv_rwlock_wrunlock(&loop->cloexec_lock);
#endif

    close(errorFds[1]);

    if (ret == -1) {
        SetErrorMessage("Could not attach to container");
    } else {
        do {
            ret = read(errorFds[0], &execErrno_, sizeof(execErrno_));
        } while (ret == -1 && errno == EINTR);

        if (ret != 0) {
            // exec failed, reap child process

            do {
                waitpid(pid_, nullptr, 0);
            } while (ret == -1 && errno == EINTR);
        }
    }

    close(errorFds[0]);
}

void AttachWorker::HandleOKCallback() {
    NanScope();

    Local<Object> attachedProcess = GetFromPersistent("attachedProcess");
    Local<Uint32> pid = NanNew<Uint32>(pid_);

    attachedProcess->Set(NanNew("pid"), pid);

    if (execErrno_ == 0) {
        Local<Object> processes = NanNew(attachedProcesses);
        processes->Set(pid_, attachedProcess);

        if (attachedProcess->Get(NanNew("_ref"))->BooleanValue()) {
            uv_ref(reinterpret_cast<uv_handle_t*>(&sigchldHandle));
        }

        const int argc = 2;
        Local<Value> argv[argc] = {
            NanNew("attach"),
            pid
        };

        Local<Function> emit = attachedProcess->Get(NanNew("emit")).As<Function>();
        NanMakeCallback(attachedProcess, emit, argc, argv);
    } else {
        // Attaching was successful but exec failed

        const int argc = 2;
        Local<Value> argv[argc] = {
            attachedProcess,
            NanNew<Int32>(-execErrno_)
        };

        exitCallback->Call(argc, argv);
    }
}

void AttachWorker::HandleErrorCallback() {
    NanScope();

    Local<Object> attachedProcess = GetFromPersistent("attachedProcess");

    const int argc = 2;
    Local<Value> argv[argc] = {
        NanNew("error"),
        NanError(ErrorMessage())
    };

    Local<Function> emit = attachedProcess->Get(NanNew("emit")).As<Function>();
    NanMakeCallback(attachedProcess, emit, argc, argv);
}

// This method gets called within the container
int AttachWorker::AttachFunction(void *payload) {
    AttachWorker *worker = static_cast<AttachWorker*>(payload);

    auto& fds = worker->fds_;
    auto& args = worker->args_;

    if (worker->term_) {
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

    // at this point execvp has failed

    int execErrno = errno;
    ssize_t ret;

    do {
        // write the exec errno back to the parent
        ret = write(worker->errorFd_, &execErrno, sizeof(execErrno));
    } while (ret == -1 && errno == EINTR);

    return 127;
}

void CreateFds(Local<Value> streams, Local<Value> term,
        std::vector<int>& childFds, std::vector<int>& parentFds) {
    int count = 3;
    int pos = 0; // TODO iterator?

    if (streams->IsUint32()) {
        count += streams->Uint32Value();
    }

    childFds.resize(count);
    parentFds.resize(count);

    if (term->BooleanValue()) {
        int master, slave;

        winsize size;
        size.ws_xpixel = 0;
        size.ws_ypixel = 0;

        Local<Object> termOptions = term->ToObject();
        Local<Value> rows = termOptions->Get(NanNew("rows"));
        Local<Value> columns = termOptions->Get(NanNew("columns"));

        size.ws_row = rows->IsUint32() ? rows->Uint32Value() : 24;
        size.ws_col = columns->IsUint32() ? columns->Uint32Value() : 80;

#ifdef UV_CLOEXEC_LOCK
        // Acquire read lock to prevent the file descriptor from leaking to
        // other processes before the FD_CLOEXEC flag is set.
        uv_loop_t *loop = uv_default_loop();
        uv_rwlock_rdlock(&loop->cloexec_lock);
#endif

        openpty(&master, &slave, nullptr, nullptr, &size);

        SetFdFlags(master, FD_CLOEXEC);
        SetFdFlags(slave, FD_CLOEXEC);

#ifdef UV_CLOEXEC_LOCK
        uv_rwlock_rdunlock(&loop->cloexec_lock);
#endif

        SetFlFlags(master, O_NONBLOCK);

        for (/* reusing fdPos */; pos < 3; pos++) {
            parentFds[pos] = master;
            childFds[pos] = slave;
        }
    }

    for (/* reusing fdPos */; pos < count; pos++) {
        int fds[2];
        socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
        SetFlFlags(fds[0], O_NONBLOCK);
        parentFds[pos] = fds[0];
        childFds[pos] = fds[1];
    }
}

// Javascript Functions

NAN_METHOD(Ref) {
    NanScope();

    if (!args[0]->IsUint32()) {
        return NanThrowTypeError("Invalid argument");
    }

    Local<Object> processes = NanNew(attachedProcesses);

    if (processes->Has(args[0]->Uint32Value())) {
        uv_ref(reinterpret_cast<uv_handle_t*>(&sigchldHandle));
    }

    NanReturnUndefined();
}

NAN_METHOD(Unref) {
    NanScope();

    if (!args[0]->IsUint32()) {
        return NanThrowTypeError("Invalid argument");
    }

    Local<Object> processes = NanNew(attachedProcesses);

    if (processes->Has(args[0]->Uint32Value())) {
        MaybeUnref();
    }

    NanReturnUndefined();
}

NAN_METHOD(Resize) {
    NanScope();

    if (!args[0]->IsUint32() || !args[1]->IsUint32() || !args[1]->IsUint32()) {
        return NanThrowTypeError("Invalid argument");
    }

    int fd = args[0]->Uint32Value();

    winsize size;
    size.ws_row = args[1]->Uint32Value();
    size.ws_col = args[2]->Uint32Value();
    size.ws_xpixel = 0;
    size.ws_ypixel = 0;

    if (ioctl(fd, TIOCSWINSZ, &size) < 0) {
        return NanThrowError(strerror(errno));
    }

    NanReturnUndefined();
}

NAN_METHOD(SetExitCallback) {
    NanScope();

    if (!args[0]->IsFunction()) {
        return NanThrowTypeError("Invalid argument");
    }

    exitCallback->SetFunction(args[0].As<Function>());

    NanReturnUndefined();
}

// Initialization

void AttachInit(Handle<Object> exports) {
    NanScope();

    NanAssignPersistent(attachedProcesses, NanNew<Object>());

    // SIGCHLD handling
    uv_signal_init(uv_default_loop(), &sigchldHandle);
    uv_signal_start(&sigchldHandle, ReapChildren, SIGCHLD);
    uv_unref(reinterpret_cast<uv_handle_t*>(&sigchldHandle));

    // Exports
    exports->Set(NanNew("ref"), NanNew<FunctionTemplate>(Ref)->GetFunction());
    exports->Set(NanNew("unref"), NanNew<FunctionTemplate>(Unref)->GetFunction());
    exports->Set(NanNew("resize"),
            NanNew<FunctionTemplate>(Resize)->GetFunction());
    exports->Set(NanNew("setExitCallback"),
            NanNew<FunctionTemplate>(SetExitCallback)->GetFunction());
}
