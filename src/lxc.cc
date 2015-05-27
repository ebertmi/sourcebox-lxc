#include "lxc.h"

#include <unistd.h>

#include <string>
#include <vector>

#include <nan.h>

#include "get.h"
#include "start.h"
#include "stop.h"
#include "destroy.h"
#include "clone.h"
#include "attach.h"

using namespace v8;

static Persistent<Function> containerConstructor;
static const pid_t pid = getpid();

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

NAN_WEAK_CALLBACK(WeakCallback) {
    lxc_container_put(data.GetParameter());
}

Local<Object> Wrap(lxc_container *container) {
    NanEscapableScope();

    Local<Object> wrap = NanNew(containerConstructor)->NewInstance();
    NanSetInternalFieldPointer(wrap, 0, container);

    NanMakeWeakPersistent(wrap, container, &WeakCallback);

    return NanEscapeScope(wrap);
}

NAN_INLINE lxc_container *Unwrap(Local<Object> object) {
    void *ptr = NanGetInternalFieldPointer(object, 0);
    return static_cast<lxc_container*>(ptr);
}

// Constructor

NAN_METHOD(LXCContainer) {
    NanScope();

    if (args.IsConstructCall()) {
        NanReturnThis();
    }

    NanReturnValue(NanNew(containerConstructor)->NewInstance());
}

// Methods

NAN_METHOD(Start) {
    NanScope();

    if (!args[0]->IsArray() || !args[1]->IsFunction()) {
        return NanThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(args.Holder());

    Local<Array> arguments = args[0].As<Array>();
    NanCallback *callback = new NanCallback(args[1].As<Function>());

    NanAsyncQueueWorker(new StartWorker(container, callback, arguments));

    NanReturnUndefined();
}

NAN_METHOD(Stop) {
    NanScope();

    if (!args[0]->IsFunction()) {
        return NanThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(args.Holder());

    NanCallback *callback = new NanCallback(args[0].As<Function>());

    NanAsyncQueueWorker(new StopWorker(container, callback));

    NanReturnUndefined();
}

NAN_METHOD(Destroy) {
    NanScope();

    if (!args[0]->IsFunction()) {
        return NanThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(args.Holder());

    NanCallback *callback = new NanCallback(args[0].As<Function>());

    NanAsyncQueueWorker(new DestroyWorker(container, callback));

    NanReturnUndefined();
}

NAN_METHOD(Clone) {
    NanScope();

    if (!args[0]->IsString() || !args[1]->IsObject() || !args[2]->IsFunction()) {
        return NanThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(args.Holder());

    Local<String> name = args[0]->ToString();
    Local<Object> options = args[1]->ToObject();
    NanCallback *callback = new NanCallback(args[2].As<Function>());

    NanAsyncQueueWorker(new CloneWorker(container, callback, name, options));

    NanReturnUndefined();
}

NAN_METHOD(Attach) {
    NanScope();

    if (!args[0]->IsFunction() || !args[1]->IsString()
            || !args[2]->IsArray() || !args[3]->IsObject()) {
        return NanThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(args.Holder());

    // command
    std::string command = *String::Utf8Value(args[1]);

    // args
    std::vector<std::string> arguments = JsArrayToVector(args[2].As<Array>());

    Local<Object> options = args[3]->ToObject();

    // env
    std::vector<std::string> env;
    Local<Value> envValue = options->Get(NanNew("env"));

    if (envValue->IsArray()) {
        env = JsArrayToVector(envValue.As<Array>());
    }

    // cwd
    std::string cwd = "/";
    Local<Value> cwdValue = options->Get(NanNew("cwd"));

    if (cwdValue->IsString()) {
        cwd = *String::Utf8Value(cwdValue);
    }

    // stdio
    std::vector<int> childFds, parentFds;

    Local<Value> streams = options->Get(NanNew("streams"));
    Local<Value> term = options->Get(NanNew("term"));

    CreateFds(streams, term, childFds, parentFds);

    Local<Array> fdArray = NanNew<Array>(parentFds.size());

    for (unsigned int i = 0; i < parentFds.size(); i++) {
        fdArray->Set(i, NanNew<Uint32>(parentFds[i]));
    }

    // create AttachedProcess instance
    Local<Function> AttachedProcess = args[0].As<Function>();

    const int argc = 4;
    Local<Value> argv[argc] = {
        args[1],
        fdArray,
        NanNew(term->BooleanValue()),
        args.Holder()->Get(NanNew("owner"))
    };

    Local<Object> attachedProcess = AttachedProcess->NewInstance(argc, argv);

    // queue worker
    AttachWorker* attachWorker = new AttachWorker(container, attachedProcess,
            command, arguments, cwd, env, childFds, term->BooleanValue());
    NanAsyncQueueWorker(attachWorker);

    NanReturnValue(attachedProcess);
}

// this is just a test, should probably be asynchronous
// TODO: test how much time this actually takes
NAN_METHOD(State) {
    NanScope();

    lxc_container *container = Unwrap(args.Holder());
    Local<String> state = NanNew(container->state(container));

    NanReturnValue(state);
}

NAN_METHOD(GetKeys) {
    NanScope();

    lxc_container *container = Unwrap(args.Holder());

    int len = container->get_keys(container, nullptr, nullptr, 0);

    if (len < 0) {
        return NanThrowError("Unable to read configuration keys");
    }

    char *buffer = new char[len + 1];

    if (container->get_keys(container, nullptr, buffer, len + 1) != len) {
        delete[] buffer;
        return NanThrowError("Unable to read configuration keys");
    }

    Local<String> keys = NanNew(buffer);
    delete[] buffer;

    NanReturnValue(keys);
}

NAN_METHOD(GetConfigItem) {
    NanScope();

    if (!args[0]->IsString()) {
        return NanThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(args.Holder());

    String::Utf8Value key(args[0]);

    int len = container->get_config_item(container, *key, nullptr, 0);

    if (len < 0) {
        return NanThrowError("Invalid configuration key");
    }

    if (len == 0) {
        NanReturnValue("");
    }

    char *buffer = new char[len + 1];

    if (container->get_config_item(container, *key, buffer, len + 1) != len) {
        delete[] buffer;
        return NanThrowError("Unable to read configuration value");
    }

    Local<String> value = NanNew<String>(buffer);
    delete[] buffer;

    NanReturnValue(value);
}

NAN_METHOD(SetConfigItem) {
    NanScope();

    if (!args[0]->IsString() || !(args[1]->IsString() || args[1]->IsNumber())) {
        return NanThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(args.Holder());

    String::Utf8Value key(args[0]);
    String::Utf8Value value(args[1]);

    bool ret = container->set_config_item(container, *key, *value);

    NanReturnValue(ret);
}

NAN_METHOD(ClearConfigItem) {
    NanScope();

    if (!args[0]->IsString()) {
        return NanThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(args.Holder());

    String::Utf8Value key(args[0]);

    bool ret = container->clear_config_item(container, *key);

    NanReturnValue(ret);
}

NAN_METHOD(GetRunningConfigItem) {
    NanScope();

    if (!args[0]->IsString()) {
        return NanThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(args.Holder());

    String::Utf8Value key(args[0]);

    char *ret = container->get_running_config_item(container, *key);

    if (!ret) {
        return NanThrowError("Unable to read configuration value");
    }

    Local<String> value = NanNew<String>(ret);
    free(ret);

    NanReturnValue(value);
}

NAN_METHOD(GetCgroupItem) {
    NanScope();

    if (!args[0]->IsString()) {
        return NanThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(args.Holder());

    String::Utf8Value key(args[0]);

    int len = container->get_cgroup_item(container, *key, nullptr, 0);

    if (len < 0) {
        return NanThrowError("Invalid cgroup key or container not running");
    }

    char *buffer = new char[len + 1];

    if (container->get_cgroup_item(container, *key, buffer, len + 1) != len) {
        delete[] buffer;
        return NanThrowError("Unable to read cgroup value");
    }

    Local<String> value = NanNew(buffer);
    delete[] buffer;

    NanReturnValue(value);
}

NAN_METHOD(SetCgroupItem) {
    NanScope();

    if (!args[0]->IsString() || !(args[1]->IsString() || args[1]->IsNumber())) {
        return NanThrowTypeError("Invalid argument");
    }

    lxc_container *container = Unwrap(args.Holder());

    String::Utf8Value key(args[0]);
    String::Utf8Value value(args[1]);

    bool ret = container->set_cgroup_item(container, *key, *value);

    NanReturnValue(ret);
}

// Get container

NAN_METHOD(GetContainer) {
    NanScope();

    if (!args[0]->IsString() && !args[1]->IsString()) {
        return NanThrowTypeError("Invalid argument");
    }

    String::Utf8Value name(args[0]);
    String::Utf8Value path(args[1]);

    NanCallback *callback = new NanCallback(args[2].As<Function>());

    NanAsyncQueueWorker(new GetWorker(callback, *name, *path, true));

    NanReturnUndefined();
}

// Initialization

void Init(Handle<Object> exports) {
    NanScope();

    on_exit(ExitHandler, nullptr);

    AttachInit(exports);

    Local<FunctionTemplate>constructorTemplate = NanNew<FunctionTemplate>(LXCContainer);

    constructorTemplate->SetClassName(NanNew("LXCContainer"));
    constructorTemplate->InstanceTemplate()->SetInternalFieldCount(1);

    // Methods
    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "start", Start);
    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "stop", Stop);
    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "destroy", Destroy);
    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "clone", Clone);

    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "state", State);
    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "attach", Attach);

    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "getKeys", GetKeys);
    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "getConfigItem", GetConfigItem);
    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "setConfigItem", SetConfigItem);
    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "clearConfigItem", ClearConfigItem);
    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "getRunningConfigItem", GetRunningConfigItem);

    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "getCgroupItem", GetCgroupItem);
    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "setCgroupItem", SetCgroupItem);

    NanAssignPersistent(containerConstructor, constructorTemplate->GetFunction());

    // Exports
    exports->Set(NanNew("getContainer"),
            NanNew<FunctionTemplate>(GetContainer)->GetFunction());

}

NODE_MODULE(lxc, Init)
