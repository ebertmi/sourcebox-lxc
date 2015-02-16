#include <lxc/lxccontainer.h>
#include <string>

#include <node.h>
#include <nan.h>

using namespace v8;

// Baton structs to be passed to the async functions

struct Baton {
    Baton() {
        req.data = this;
    }

    virtual ~Baton() {
        delete callback;
    }

    uv_work_t req;

    NanCallback *callback;

    bool error = false;
    std::string error_msg;

    lxc_container *container;
};

struct NewBaton : Baton {
    std::string name;
    std::string configpath;
};

// Helper, Cleanup etc.

NAN_WEAK_CALLBACK(weakCallback) {
    lxc_container_put(data.GetParameter());
}

NAN_INLINE void makeErrorCallback(const Baton *baton) {
    unsigned int argc = 1;

    Local<Value> argv[argc] = {
        NanError(baton->error_msg.c_str())
    };

    baton->callback->Call(argc, argv);
}

NAN_INLINE lxc_container *unwrap(_NAN_METHOD_ARGS) {
    void *ptr = NanGetInternalFieldPointer(args.Holder(), 0);
    return static_cast<lxc_container*>(ptr);
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

void newContainerAsync(uv_work_t *req) {
    NewBaton *baton = static_cast<NewBaton*>(req->data);

    lxc_container *container = lxc_container_new(baton->name.c_str(),
            baton->configpath.c_str());

    if (!container) {
        baton->error_msg = "Failed to create container";
        baton->error = true;
    } else if (!container->is_defined(container)) {
        baton->error_msg = "Container not found";
        baton->error = true;
        lxc_container_put(container);
    } else {
        baton->container = container;
    }
}

void newContainerAfter(uv_work_t *req, int status) {
    NanScope();

    Baton *baton = static_cast<Baton*>(req->data);

    if (baton->error) {
        makeErrorCallback(baton);
    } else {
        unsigned int argc = 2;

        Local<Object> container = NanNew(constructor)->NewInstance();
        NanSetInternalFieldPointer(container, 0, baton->container);

        NanMakeWeakPersistent(container, baton->container, &weakCallback);

        Local<Value> argv[argc] = {
            NanNull(),
            container
        };

        baton->callback->Call(argc, argv);
    }

    delete baton;
}

NAN_METHOD(newContainer) {
    NanScope();

    NewBaton *baton = new NewBaton;

    baton->name = std::string(*String::Utf8Value(args[0]));
    baton->configpath = std::string(*String::Utf8Value(args[1]));

    baton->callback = new NanCallback(args[2].As<Function>());

    uv_queue_work(uv_default_loop(), &baton->req,
            newContainerAsync, newContainerAfter);

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

    NanReturnValue(NanNew(ret));
}

// this is just a test, should probably be asynchronous
NAN_METHOD(state) {
    NanScope();

    lxc_container *c = unwrap(args);
    Local<String> state = NanNew(c->state(c));

    NanReturnValue(state);
}

// Initialization

void init(Handle<Object> exports, Handle<Object> module) {
    NanScope();

    Local<FunctionTemplate>constructorTemplate = NanNew<FunctionTemplate>(LXCContainer);

    constructorTemplate->SetClassName(NanNew("LXCContainer"));
    constructorTemplate->InstanceTemplate()->SetInternalFieldCount(1);

    // Methods
    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "state", state);
    NODE_SET_PROTOTYPE_METHOD(constructorTemplate, "start", start);

    NanAssignPersistent(constructor, constructorTemplate->GetFunction());

    // Exports
    module->Set(NanNew("exports"), NanNew<FunctionTemplate>(newContainer)->GetFunction());
}

NODE_MODULE(lxc, init);
