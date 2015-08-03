#ifndef SOURCEBOX_ATTACH_H
#define SOURCEBOX_ATTACH_H

#include <vector>

#include "async.h"

class AttachCommand {
public:
    virtual ~AttachCommand() {};

    virtual int Attach(int errorFd);
    virtual int Attach();
};

class AttachWorker : public LxcWorker {
public:
    AttachWorker(lxc_container *container, v8::Local<v8::Object> attachedProcess,
            AttachCommand *command, const std::string& cwd,
            const std::vector<std::string>& env,
            const std::vector<int>& fds, bool term,
            int namespaces, bool cgroup, int uid, int gid);

    ~AttachWorker();

private:
    void LxcExecute() override;
    void HandleOKCallback() override;
    void HandleErrorCallback() override;

    static int AttachFunction(void *payload);

    int pid_;
    int execErrno_ = 0;

    AttachCommand *command_;
    std::string cwd_;
    std::vector<char*> env_;
    std::vector<int> fds_;
    bool term_;
    bool cgroup_;
    int namespaces_;
    int uid_;
    int gid_;
    int errorFd_;
};

class ExecCommand : public AttachCommand {
public:
    ExecCommand(const std::string& command,
            const std::vector<std::string>& args);

    ~ExecCommand();

    int Attach(int errorFd) override;

private:
    std::vector<char*> args_;
};

class OpenCommand : public AttachCommand {
public:
    OpenCommand(const std::string& path, int flags, int mode)
        : path_(path), flags_(flags), mode_(mode) {}

    int Attach() override;

private:
    static void InitialCleanup();

    std::string path_;
    int flags_;
    int mode_;
};

void CreateFds(v8::Local<v8::Value> streams, v8::Local<v8::Value> term,
        std::vector<int>& childFds, std::vector<int>& parentFds);

void AttachInit(v8::Handle<v8::Object> exports);

#endif
