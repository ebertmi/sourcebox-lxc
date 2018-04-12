#include "attach.h"

#include <dirent.h>
#include <pty.h>
#include <sys/capability.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utmp.h>

#if NAUV_UVVERSION >= 0x000b14
#define HAVE_UV_CLOEXEC_LOCK
#endif

using namespace v8;

Nan::Callback *exitCallback = new Nan::Callback();
static Nan::Persistent<Object> attachedProcesses;
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
    Nan::HandleScope scope;

    Local<Object> processes = Nan::New(attachedProcesses);
    Local<Array> pids = processes->GetOwnPropertyNames();
    int length = pids->Length();

    for (int i = 0; i < length; i++) {
        Local<Object> process = processes->Get(pids->Get(i))->ToObject();
        if (process->Get(Nan::New("_ref").ToLocalChecked())->BooleanValue()) {
            // at least one process is still referenced
            return;
        }
    }

    uv_unref(reinterpret_cast<uv_handle_t*>(&sigchldHandle));
}

static void ReapChildren(uv_signal_t* handle, int signal) {
    Nan::HandleScope scope;

    Local<Object> processes = Nan::New(attachedProcesses);
    Local<Array> pids = processes->GetOwnPropertyNames();
    int length = pids->Length();

    std::vector<std::pair<int,int>> reaped;

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
            if (errno == ECHILD) {
                // child already was reaped, this happens on old node versions
                reaped.push_back(std::make_pair(-pid, 0));
                continue;
            }

            abort();
        }

        reaped.push_back(std::make_pair(pid, status));
    }

    for (auto pair : reaped) {
        int pid = pair.first;
        int status = pair.second;

        Local<Value> exitCode;
        Local<Value> signalCode;

        if (pid < 0) {
            // old node version, we don't know the exit code :C
            pid = -pid;
            exitCode = Nan::Null();
            signalCode = Nan::New("ECHILD").ToLocalChecked();
        } else {
            if (WIFEXITED(status)) {
                exitCode = Nan::New<Uint32>(WEXITSTATUS(status));
                signalCode = Nan::Null();
            } else if (WIFSIGNALED(status)) {
                signalCode = Nan::New(node::signo_string(WTERMSIG(status))).ToLocalChecked();
                exitCode = Nan::Null();
            } else {
                // child process got stopped or continued
                continue;
            }
        }

        const int argc = 3;
        Local<Value> argv[argc] = {
            processes->Get(pid),
            exitCode,
            signalCode
        };

        processes->Delete(pid);
        exitCallback->Call(argc, argv);
    }

    if (!reaped.empty()) {
        MaybeUnref();
    }
}

AttachWorker::AttachWorker(lxc_container *container, Local<Object> attachedProcess,
        AttachCommand *command, const std::string& cwd,
        const std::vector<std::string>& env,
        const std::vector<int>& fds, bool term, int namespaces,
        bool cgroup, int uid, int gid)
        : LxcWorker(container, nullptr), command_(command), cwd_(cwd),
        fds_(fds), term_(term), cgroup_(cgroup),
        namespaces_(namespaces), uid_(uid), gid_(gid) {
    Nan::HandleScope scope;

    SaveToPersistent("attachedProcess", attachedProcess);

    env_.resize(env.size() + 1);
    env_.back() = nullptr;

    for (unsigned int i = 0; i < env_.size() - 1; i++) {
        env_[i] = strdup(env[i].c_str());
    }
}

AttachWorker::~AttachWorker() {
    // env
    for (char *p: env_) {
        free(p);
    }

    // stdio
    for (int fd: fds_) {
        close(fd);
    }

    // command
    delete command_;
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

    if (!cgroup_) {
        options.attach_flags &= ~LXC_ATTACH_MOVE_TO_CGROUP; //FIXME i think thats wrong
    }

    options.namespaces = namespaces_;

    int errorFds[2];
    socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, errorFds);
    errorFd_ = errorFds[1];

#ifdef HAVE_UV_CLOEXEC_LOCK
    // Acquire write lock to prevent opening new FDs in other threads.
    uv_loop_t *loop = uv_default_loop();
    uv_rwlock_wrlock(&loop->cloexec_lock);
#endif

    int ret = container_->attach(container_, AttachFunction, this, &options, &pid_);

#ifdef HAVE_UV_CLOEXEC_LOCK
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
                ret = waitpid(pid_, nullptr, 0);
            } while (ret == -1 && errno == EINTR);
        }
    }

    close(errorFds[0]);
}

void AttachWorker::HandleOKCallback() {
    Nan::HandleScope scope;

    Local<Object> attachedProcess = GetFromPersistent("attachedProcess")->ToObject();
    Local<Uint32> pid = Nan::New<Uint32>(pid_);

    attachedProcess->Set(Nan::New("pid").ToLocalChecked(), pid);

    if (execErrno_ == 0) {
        Local<Object> processes = Nan::New(attachedProcesses);
        processes->Set(pid_, attachedProcess);

        if (attachedProcess->Get(Nan::New("_ref").ToLocalChecked())->BooleanValue()) {
            uv_ref(reinterpret_cast<uv_handle_t*>(&sigchldHandle));
        }

        const int argc = 2;
        Local<Value> argv[argc] = {
            Nan::New("attach").ToLocalChecked(),
            pid
        };

        Local<Function> emit = attachedProcess->Get(Nan::New("emit").ToLocalChecked()).As<Function>();
        Nan::MakeCallback(attachedProcess, emit, argc, argv);
        // ToDo: replace with 
        /*
            Nan::AsyncResource* async_resource;
            async_resource = new Nan::AsyncResource("sourcebox-lxc:HandleOKCallback");
            async_resource.runInAsyncScope(attachedProcess, emit, argc, argv);
            delete async_resource;

        */
        ReapChildren(nullptr, 0);
    } else {
        // Attaching was successful but exec failed

        const int argc = 2;
        Local<Value> argv[argc] = {
            attachedProcess,
            Nan::New<Int32>(-execErrno_)
        };

        exitCallback->Call(argc, argv);
    }
}

void AttachWorker::HandleErrorCallback() {
    Nan::HandleScope scope;

    Local<Object> attachedProcess = GetFromPersistent("attachedProcess")->ToObject();

    const int argc = 2;
    Local<Value> argv[argc] = {
        Nan::New("error").ToLocalChecked(),
        Nan::Error(ErrorMessage())
    };

    Local<Function> emit = attachedProcess->Get(Nan::New("emit").ToLocalChecked()).As<Function>();
    Nan::MakeCallback(attachedProcess, emit, argc, argv);
}

// This method gets called within the container
int AttachWorker::AttachFunction(void *payload) {
    AttachWorker *worker = static_cast<AttachWorker*>(payload);

    auto& fds = worker->fds_;

    if (worker->term_) {
        login_tty(0);
    } else {
        setsid();
    }

    for (unsigned int i = 3; i < fds.size(); i++) {
        unsigned int fd = fds[i];
        dup2(fd, i);
        if (fd != i) {
            close(fd);
        }
    }

    return worker->command_->Attach(worker->errorFd_);
}

int AttachCommand::Attach(int errorFd) {
    close(errorFd);
    return Attach();
}

int AttachCommand::Attach() {
    return EXIT_SUCCESS;
}

ExecCommand::ExecCommand(const std::string& command,
        const std::vector<std::string>& args) {
    Nan::HandleScope scope;

    args_.resize(args.size() + 2);
    args_.front() = strdup(command.c_str());
    args_.back() = nullptr;

    for (unsigned int i = 0; i < args_.size() - 2; i++) {
        args_[i + 1] = strdup(args[i].c_str());
    }
}

ExecCommand::~ExecCommand() {
    for (char *p: args_) {
        free(p);
    }
}

int ExecCommand::Attach(int errorFd) {
    execvp(args_.front(), args_.data());

    // at this point execvp has failed
    int execErrno = errno;
    ssize_t ret;

    do {
        // write the exec errno back to the parent
        ret = write(errorFd, &execErrno, sizeof(execErrno));
    } while (ret == -1 && errno == EINTR);

    close(errorFd);

    return 127;
}

int OpenCommand::Attach() {
    InitialCleanup();

    int fd = open(path_.c_str(), flags_, mode_);

    if (fd < 0) {
        fprintf(stderr, "%d", errno);
        fflush(stderr);
        fclose(stderr);

        return EXIT_FAILURE;
    }

    printf("%d", fd);
    fflush(stdout);
    fclose(stdout);

    // Wait until node has read the FD and signaled this by closing stdin.
    while (getchar() != EOF);

    return EXIT_SUCCESS;
}

void OpenCommand::InitialCleanup() {
    // Do some cleanup that normally gets done automatically by execve(). But
    // we are not execing anything here.

    // Drop all capabilites if we are not root.
    if (geteuid() != 0) {
        cap_t caps = cap_get_proc();
        cap_clear(caps);
        cap_set_proc(caps);
        cap_free(caps);
    }

    // Close all FDs >= 3.
    DIR *dir = opendir("/proc/self/fd");

    if (dir) {
        dirent *de;

        while ((de = readdir(dir))) {
            char *name = de->d_name;

            if (name[0] == '.') {
                continue;
            }

            int fd = atoi(name);

            if (fd >= 3) {
                close(fd);
            }
        }

        closedir(dir);
    } else {
        int maxfd = getdtablesize();

        for (int fd = 3; fd < maxfd; fd++) {
            close(fd);
        }
    }
}

void CreateFds(Local<Value> streams, Local<Value> term,
        std::vector<int>& childFds, std::vector<int>& parentFds) {
    Nan::HandleScope scope;

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
        Local<Value> rows = termOptions->Get(Nan::New("rows").ToLocalChecked());
        Local<Value> columns = termOptions->Get(Nan::New("columns").ToLocalChecked());

        size.ws_row = rows->IsUint32() ? rows->Uint32Value() : 24;
        size.ws_col = columns->IsUint32() ? columns->Uint32Value() : 80;

#ifdef HAVE_UV_CLOEXEC_LOCK
        // Acquire read lock to prevent the file descriptor from leaking to
        // other processes before the FD_CLOEXEC flag is set.
        uv_loop_t *loop = uv_default_loop();
        uv_rwlock_rdlock(&loop->cloexec_lock);
#endif

        openpty(&master, &slave, nullptr, nullptr, &size);

        SetFdFlags(master, FD_CLOEXEC);
        SetFdFlags(slave, FD_CLOEXEC);

#ifdef HAVE_UV_CLOEXEC_LOCK
        uv_rwlock_rdunlock(&loop->cloexec_lock);
#endif

        SetFlFlags(master, O_NONBLOCK);

        for (/* reusing pos */; pos < 3; pos++) {
            parentFds[pos] = master;
            childFds[pos] = slave;
        }
    }

    for (/* reusing pos */; pos < count; pos++) {
        int fds[2];
        socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds);
        SetFlFlags(fds[0], O_NONBLOCK);
        parentFds[pos] = fds[0];
        childFds[pos] = fds[1];
    }
}

// Javascript Functions

NAN_METHOD(Ref) {
    if (!info[0]->IsUint32()) {
        return Nan::ThrowTypeError("Invalid argument");
    }

    Local<Object> processes = Nan::New(attachedProcesses);

    if (processes->Has(info[0]->Uint32Value())) {
        uv_ref(reinterpret_cast<uv_handle_t*>(&sigchldHandle));
    }
}

NAN_METHOD(Unref) {
    if (!info[0]->IsUint32()) {
        return Nan::ThrowTypeError("Invalid argument");
    }

    Local<Object> processes = Nan::New(attachedProcesses);

    if (processes->Has(info[0]->Uint32Value())) {
        MaybeUnref();
    }
}

NAN_METHOD(Resize) {
    if (!info[0]->IsUint32() || !info[1]->IsUint32() || !info[1]->IsUint32()) {
        return Nan::ThrowTypeError("Invalid argument");
    }

    int fd = info[0]->Uint32Value();

    winsize size;
    size.ws_col = info[1]->Uint32Value();
    size.ws_row = info[2]->Uint32Value();
    size.ws_xpixel = 0;
    size.ws_ypixel = 0;

    if (ioctl(fd, TIOCSWINSZ, &size) < 0) {
        return Nan::ThrowError(strerror(errno));
    }
}

NAN_METHOD(SetExitCallback) {
    if (!info[0]->IsFunction()) {
        return Nan::ThrowTypeError("Invalid argument");
    }

    exitCallback->SetFunction(info[0].As<Function>());
}

// Initialization

void AttachInit(Handle<Object> exports) {
    Nan::HandleScope scope;

    attachedProcesses.Reset(Nan::New<Object>());

    // SIGCHLD handling
    uv_signal_init(uv_default_loop(), &sigchldHandle);
    uv_signal_start(&sigchldHandle, ReapChildren, SIGCHLD);
    uv_unref(reinterpret_cast<uv_handle_t*>(&sigchldHandle));

    // Exports
    exports->Set(Nan::New("ref").ToLocalChecked(),
            Nan::New<FunctionTemplate>(Ref)->GetFunction());
    exports->Set(Nan::New("unref").ToLocalChecked(),
            Nan::New<FunctionTemplate>(Unref)->GetFunction());
    exports->Set(Nan::New("resize").ToLocalChecked(),
            Nan::New<FunctionTemplate>(Resize)->GetFunction());
    exports->Set(Nan::New("setExitCallback").ToLocalChecked(),
            Nan::New<FunctionTemplate>(SetExitCallback)->GetFunction());
}
