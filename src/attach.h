#ifndef SOURCEBOX_ATTACH_H
#define SOURCEBOX_ATTACH_H

#include <vector>

#include "async.h"

class AttachWorker : public LxcWorker {
public:
    AttachWorker(lxc_container *container, v8::Local<v8::Object> attachedProcess,
            const std::string& command, const std::vector<std::string>& args,
            const std::string& cwd, const std::vector<std::string>& env,
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

    std::string cwd_;
    std::vector<char*> args_;
    std::vector<char*> env_;
    std::vector<int> fds_;
    bool term_;
    bool cgroup_;
    int namespaces_;
    int uid_;
    int gid_;
    int errorFd_;
};

void CreateFds(v8::Local<v8::Value> streams, v8::Local<v8::Value> term,
        std::vector<int>& childFds, std::vector<int>& parentFds);

void AttachInit(v8::Handle<v8::Object> exports);

#endif
