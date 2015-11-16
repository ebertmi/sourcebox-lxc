#include "lxc.h"

#include <unistd.h>
#include <sched.h>

#include <string>
#include <vector>
#include <map>

#include <nan.h>

#include "get.h"
#include "create.h"
#include "clone.h"
#include "config.h"
#include "destroy.h"
#include "start.h"
#include "stop.h"
#include "attach.h"

using namespace v8;

static Nan::Persistent<Function> containerConstructor;
static const pid_t pid = getpid();

static const std::map<std::string, int> nsMap = {
    {"ns", CLONE_NEWNS},
    {"mount", CLONE_NEWNS},
    {"uts", CLONE_NEWUTS},
    {"ipc", CLONE_NEWIPC},
    {"user", CLONE_NEWUSER},
    {"pid", CLONE_NEWPID},
    {"net", CLONE_NEWNET},
};

// Helper, Cleanup etc.

static std::vector<std::string> JsArrayToVector(const Local<Array> source) {
    std::vector<std::string> dest(source->Length());

    for (unsigned int i = 0; i < dest.size(); i++) {
        dest[i] = *String::Utf8Value(source->Get(i));
    }

    return dest;
}

static void ExitHandler(int status, void *) {
    if (getpid() != pid) {
        // this is not the main process, exit right away
        _exit(status);
    }
}

void WeakCallback(const Nan::WeakCallbackInfo<lxc_container> &data) {
    lxc_container_put(data.GetParameter());
}

Local<Object> Wrap(lxc_container *container) {
    Nan::EscapableHandleScope scope;

    Local<Object> wrap = Nan::New(containerConstructor)->NewInstance();
    Nan::SetInternalFieldPointer(wrap, 0, container);

    Nan::Persistent<Object> persistent(wrap);
    persistent.SetWeak(container, WeakCallback, Nan::WeakCallbackType::kParameter);

    return scope.Escape(wrap);
}

NAN_INLINE lxc_container *Unwrap(Local<Object> object) {
    void *ptr = Nan::GetInternalFieldPointer(object, 0);
    return static_cast<lxc_container*>(ptr);
}

// Constructor

NAN_METHOD(LXCContainer) {
    if (info.IsConstructCall()) {
        return info.GetReturnValue().Set(info.Holder());
    }

    info.GetReturnValue().Set(Nan::New(containerConstructor)->NewInstance());
}

// Methods

NAN_METHOD(Start) {
    if (!info[0]->IsArray() || !info[1]->IsFunction()) {
        return Nan::ThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(info.Holder());

    Local<Array> arguments = info[0].As<Array>();
    Nan::Callback *callback = new Nan::Callback(info[1].As<Function>());

    Nan::AsyncQueueWorker(new StartWorker(container, callback, arguments));
}

NAN_METHOD(Create) {
    if (!info[0]->IsString() || !info[1]->IsString() || !info[2]->IsArray()
            || !info[3]->IsFunction()) {
        return Nan::ThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(info.Holder());

    Nan::Callback *callback = new Nan::Callback(info[3].As<Function>());

    CreateWorker *worker = new CreateWorker(container, callback,
            *String::Utf8Value(info[0]), *String::Utf8Value(info[1]),
            JsArrayToVector(info[2].As<Array>()));

    Nan::AsyncQueueWorker(worker);
}

NAN_METHOD(Stop) {
    if (!info[0]->IsFunction()) {
        return Nan::ThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(info.Holder());

    Nan::Callback *callback = new Nan::Callback(info[0].As<Function>());

    Nan::AsyncQueueWorker(new StopWorker(container, callback));
}

NAN_METHOD(Destroy) {
    if (!info[0]->IsFunction()) {
        return Nan::ThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(info.Holder());

    Nan::Callback *callback = new Nan::Callback(info[0].As<Function>());

    Nan::AsyncQueueWorker(new DestroyWorker(container, callback));
}

NAN_METHOD(Clone) {
    if (!info[0]->IsString() || !info[1]->IsObject() || !info[2]->IsFunction()) {
        return Nan::ThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(info.Holder());

    Local<String> name = info[0]->ToString();
    Local<Object> options = info[1]->ToObject();
    Nan::Callback *callback = new Nan::Callback(info[2].As<Function>());

    Nan::AsyncQueueWorker(new CloneWorker(container, callback, name, options));
}

NAN_METHOD(Attach) {
    if (!info[0]->IsFunction() || !info[1]->IsString()
            || !info[2]->IsArray() || !info[3]->IsObject()) {
        return Nan::ThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(info.Holder());

    // command
    std::string command = *String::Utf8Value(info[1]);

    // args
    std::vector<std::string> arguments = JsArrayToVector(info[2].As<Array>());

    Local<Object> options = info[3]->ToObject();

    // env
    std::vector<std::string> env;
    Local<Value> envValue = options->Get(Nan::New("env").ToLocalChecked());

    if (envValue->IsArray()) {
        env = JsArrayToVector(envValue.As<Array>());
    }

    // cwd
    std::string cwd = "/";
    Local<Value> cwdValue = options->Get(Nan::New("cwd").ToLocalChecked());

    if (cwdValue->IsString()) {
        cwd = *String::Utf8Value(cwdValue);
    }

    // uid & gid
    int uid = -1;
    Local<Value> uidValue = options->Get(Nan::New("uid").ToLocalChecked());

    if (uidValue->IsUint32()) {
        uid = uidValue->Uint32Value();
    }

    int gid = -1;
    Local<Value> gidValue = options->Get(Nan::New("gid").ToLocalChecked());

    if (gidValue->IsUint32()) {
        gid = gidValue->Uint32Value();
    }

    // cgroup
    bool cgroup = true;
    Local<Value> cgroupValue = options->Get(Nan::New("cgroup").ToLocalChecked());

    if (cgroupValue->IsBoolean()) {
        cgroup = cgroupValue->BooleanValue();
    }

    // namespaces
    int namespaces = -1;
    Local<Value> nsValue = options->Get(Nan::New("namespaces").ToLocalChecked());

    if (nsValue->IsArray()) {
        Local<Array> nsArray = nsValue.As<Array>();
        int length = nsArray->Length();
        namespaces = 0;

        for (int i = 0; i < length; i++) {
            std::string ns = *String::Utf8Value(nsArray->Get(i));
            auto it = nsMap.find(ns);
            if (it != nsMap.end()) {
                namespaces |= it->second;
            } else {
                return Nan::ThrowTypeError(("invalid namespace: " + ns).c_str());
            }
        }
    }

    // stdio
    std::vector<int> childFds, parentFds;

    Local<Value> streams = options->Get(Nan::New("streams").ToLocalChecked());
    Local<Value> term = options->Get(Nan::New("term").ToLocalChecked());

    CreateFds(streams, term, childFds, parentFds);

    Local<Array> fdArray = Nan::New<Array>(parentFds.size());

    for (unsigned int i = 0; i < parentFds.size(); i++) {
        fdArray->Set(i, Nan::New<Uint32>(parentFds[i]));
    }

    // create AttachedProcess instance
    Local<Function> AttachedProcess = info[0].As<Function>();

    const int argc = 4;
    Local<Value> argv[argc] = {
        info[1],
        fdArray,
        Nan::New(term->BooleanValue()),
        info.Holder()->Get(Nan::New("owner").ToLocalChecked())
    };

    Local<Object> attachedProcess = AttachedProcess->NewInstance(argc, argv);

    // queue worker
    AttachWorker* attachWorker = new AttachWorker(container, attachedProcess,
            new ExecCommand(command, arguments), cwd, env, childFds, term->BooleanValue(),
            namespaces, cgroup, uid, gid);
    Nan::AsyncQueueWorker(attachWorker);

    info.GetReturnValue().Set(attachedProcess);
}

NAN_METHOD(OpenFile) {
    if (!info[0]->IsFunction() || !info[1]->IsString() || !info[2]->IsUint32() ||
            !info[3]->IsUint32() || !info[4]->IsUint32() || !info[5]->IsUint32()) {
        return Nan::ThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(info.Holder());

    std::string path = *String::Utf8Value(info[1]);
    int flags = info[2]->Uint32Value();
    int mode = info[3]->Uint32Value();
    int uid = info[4]->Uint32Value();
    int gid = info[5]->Uint32Value();

    OpenCommand *openCommand = new OpenCommand(path, flags, mode);

    // stdio
    std::vector<int> childFds, parentFds;
    CreateFds(Nan::New(0), Nan::New(false), childFds, parentFds);

    Local<Array> fdArray = Nan::New<Array>(parentFds.size());

    for (unsigned int i = 0; i < parentFds.size(); i++) {
        fdArray->Set(i, Nan::New<Uint32>(parentFds[i]));
    }

    // create AttachedProcess instance
    Local<Function> AttachedProcess = info[0].As<Function>();

    const int argc = 4;
    Local<Value> argv[argc] = {
        Nan::New("OpenCommand").ToLocalChecked(),
        fdArray,
        Nan::New(false),
        info.Holder()->Get(Nan::New("owner").ToLocalChecked())
    };

    Local<Object> attachedProcess = AttachedProcess->NewInstance(argc, argv);

    // queue worker
    AttachWorker* attachWorker = new AttachWorker(container, attachedProcess,
            openCommand, "/", std::vector<std::string>(), childFds, false,
            CLONE_NEWNS | CLONE_NEWUSER, false, uid, gid);
    Nan::AsyncQueueWorker(attachWorker);

    info.GetReturnValue().Set(attachedProcess);
}

NAN_METHOD(ConfigFile) {
    if (!info[0]->IsString() || !info[1]->IsBoolean()
            || !info[2]->IsFunction()) {
        return Nan::ThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(info.Holder());

    Nan::Callback *callback = new Nan::Callback(info[2].As<Function>());

    String::Utf8Value file(info[0]);
    bool save = info[1]->BooleanValue();

    Nan::AsyncQueueWorker(new ConfigWorker(container, callback, *file, save));
}

NAN_METHOD(GetKeys) {
    lxc_container *container = Unwrap(info.Holder());

    int len = container->get_keys(container, nullptr, nullptr, 0);

    if (len < 0) {
        return Nan::ThrowError("Unable to read configuration keys");
    }

    char *buffer = new char[len + 1];

    if (container->get_keys(container, nullptr, buffer, len + 1) == len) {
        Local<String> keys = Nan::New(buffer).ToLocalChecked();
        info.GetReturnValue().Set(keys);
    } else {
        Nan::ThrowError("Unable to read configuration keys");
    }

    // always free buffer (no return in front of throw)
    delete[] buffer;
}

NAN_METHOD(GetConfigItem) {
    if (!info[0]->IsString()) {
        return Nan::ThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(info.Holder());

    String::Utf8Value key(info[0]);
    int len = container->get_config_item(container, *key, nullptr, 0);

    if (len < 0) {
        return Nan::ThrowError("Invalid configuration key");
    }

    if (len == 0) {
        return info.GetReturnValue().SetEmptyString();
    }

    char *buffer = new char[len + 1];

    if (container->get_config_item(container, *key, buffer, len + 1) == len) {
        Local<String> value = Nan::New<String>(buffer).ToLocalChecked();
        info.GetReturnValue().Set(value);
    } else {
        Nan::ThrowError("Unable to read configuration value");
    }

    // always free buffer (no return in front of throw)
    delete[] buffer;
}

NAN_METHOD(SetConfigItem) {
    if (!info[0]->IsString() || !(info[1]->IsString() || info[1]->IsNumber())) {
        return Nan::ThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(info.Holder());

    String::Utf8Value key(info[0]);
    String::Utf8Value value(info[1]);

    if (!container->set_config_item(container, *key, *value)) {
        Nan::ThrowError("Unable to set configuration value");
    }
}

NAN_METHOD(ClearConfigItem) {
    if (!info[0]->IsString()) {
        return Nan::ThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(info.Holder());

    String::Utf8Value key(info[0]);

    if (!container->clear_config_item(container, *key)) {
        Nan::ThrowError("Unable to clear configuration value");
    }
}

NAN_METHOD(GetRunningConfigItem) {
    if (!info[0]->IsString()) {
        return Nan::ThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(info.Holder());

    String::Utf8Value key(info[0]);

    char *ret = container->get_running_config_item(container, *key);

    if (!ret) {
        return Nan::ThrowError("Unable to read configuration value");
    }

    Local<String> value = Nan::New(ret).ToLocalChecked();
    info.GetReturnValue().Set(value);

    free(ret);
}

NAN_METHOD(GetCgroupItem) {
    if (!info[0]->IsString()) {
        return Nan::ThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(info.Holder());

    String::Utf8Value key(info[0]);

    int len = container->get_cgroup_item(container, *key, nullptr, 0);

    if (len < 0) {
        return Nan::ThrowError("Invalid cgroup key or container not running");
    }

    char *buffer = new char[len + 1];

    if (container->get_cgroup_item(container, *key, buffer, len + 1) == len) {
        Local<String> value = Nan::New(buffer).ToLocalChecked();
        info.GetReturnValue().Set(value);
    } else {
        Nan::ThrowError("Unable to read cgroup value");
    }

    // always free buffer (no return in front of throw)
    delete[] buffer;
}

NAN_METHOD(SetCgroupItem) {
    if (!info[0]->IsString() || !(info[1]->IsString() || info[1]->IsNumber())) {
        return Nan::ThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(info.Holder());

    String::Utf8Value key(info[0]);
    String::Utf8Value value(info[1]);

    if (!container->set_cgroup_item(container, *key, *value)) {
        Nan::ThrowError("Unable to set cgroup value");
    }
}

// Get container

NAN_METHOD(GetContainer) {
    if (!info[0]->IsString() && !info[1]->IsString() && !info[2]->IsBoolean()
            && !info[3]->IsFunction()) {
        return Nan::ThrowTypeError("Invalid argument");
    }

    String::Utf8Value name(info[0]);
    String::Utf8Value path(info[1]);
    bool defined = info[2]->BooleanValue();

    Nan::Callback *callback = new Nan::Callback(info[3].As<Function>());

    Nan::AsyncQueueWorker(new GetWorker(callback, *name, *path, defined));
}

// Initialization

void Init(Handle<Object> exports) {
    Nan::HandleScope scope;

    on_exit(ExitHandler, nullptr);

    AttachInit(exports);

    Local<FunctionTemplate>constructorTemplate = Nan::New<FunctionTemplate>(LXCContainer);

    constructorTemplate->SetClassName(Nan::New("LXCContainer").ToLocalChecked());
    constructorTemplate->InstanceTemplate()->SetInternalFieldCount(1);

    // Methods
    Nan::SetPrototypeMethod(constructorTemplate, "create", Create);
    Nan::SetPrototypeMethod(constructorTemplate, "destroy", Destroy);
    Nan::SetPrototypeMethod(constructorTemplate, "clone", Clone);

    Nan::SetPrototypeMethod(constructorTemplate, "start", Start);
    Nan::SetPrototypeMethod(constructorTemplate, "stop", Stop);

    Nan::SetPrototypeMethod(constructorTemplate, "attach", Attach);

    Nan::SetPrototypeMethod(constructorTemplate, "configFile", ConfigFile);
    Nan::SetPrototypeMethod(constructorTemplate, "getKeys", GetKeys);
    Nan::SetPrototypeMethod(constructorTemplate, "getConfigItem", GetConfigItem);
    Nan::SetPrototypeMethod(constructorTemplate, "setConfigItem", SetConfigItem);
    Nan::SetPrototypeMethod(constructorTemplate, "clearConfigItem", ClearConfigItem);
    Nan::SetPrototypeMethod(constructorTemplate, "getRunningConfigItem", GetRunningConfigItem);

    Nan::SetPrototypeMethod(constructorTemplate, "getCgroupItem", GetCgroupItem);
    Nan::SetPrototypeMethod(constructorTemplate, "setCgroupItem", SetCgroupItem);

    Nan::SetPrototypeMethod(constructorTemplate, "openFile", OpenFile);

    containerConstructor.Reset(constructorTemplate->GetFunction());

    // Exports
    exports->Set(Nan::New("getContainer").ToLocalChecked(),
            Nan::New<FunctionTemplate>(GetContainer)->GetFunction());
    exports->Set(Nan::New("version").ToLocalChecked(),
            Nan::New(lxc_get_version()).ToLocalChecked());
}

NODE_MODULE(lxc, Init)
