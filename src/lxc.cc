#include <lxc/lxccontainer.h>
#include <node.h>
#include <string>

using namespace v8;

struct Baton {
    virtual ~Baton() {}

    uv_work_t req;
    Persistent<Function> callback;

    bool error;
    std::string error_msg;

    lxc_container *container;
};

struct NewBaton : Baton {
    std::string name;
    std::string configpath;
};

// Helper, Cleanup etc.

void weakCallback(Persistent<Value> persistent, void *parameter) {
    lxc_container *c = static_cast<lxc_container*>(parameter);
    lxc_container_put(c);

    persistent.Dispose();
}

lxc_container *unwrap(const Arguments& args) {
    void *ptr = args.Holder()->GetPointerFromInternalField(0);
    return static_cast<lxc_container*>(ptr);
}

// Constructor

Persistent<Function> constructor;

Handle<Value> LXCContainer(const Arguments& args) {
    if (args.IsConstructCall()) {
        return args.This();
    }

    return constructor->NewInstance();
}

// New container

void newContainerAsync(uv_work_t *req) {
    NewBaton *baton = static_cast<NewBaton*>(req->data);

    baton->container = lxc_container_new(baton->name.c_str(),
            baton->configpath.c_str());
    baton->error = !baton->container;

    if (baton->error) {
        baton->error_msg = std::string("Failed to create container");
    }
}

void newContainerAfter(uv_work_t *req, int status) {
    HandleScope scope;

    Baton *baton = static_cast<Baton*>(req->data);

    if (baton->error) {
        unsigned int argc = 1;

        Local<Value> argv[argc] = {
            Exception::Error(String::New(baton->error_msg.c_str()))
        };

        baton->callback->Call(Context::GetCurrent()->Global(), argc, argv);
    } else {
        unsigned int argc = 2;

        Local<Object> container = constructor->NewInstance();
        container->SetPointerInInternalField(0, baton->container);

        Persistent<Object> persistent = Persistent<Object>::New(container);
        persistent.MakeWeak(baton->container, &weakCallback);

        Local<Value> argv[argc] = {
            Local<Value>::New(Null()),
            container
        };

        baton->callback->Call(Context::GetCurrent()->Global(), argc, argv);
    }

    baton->callback.Dispose();
    delete baton;
}

Handle<Value> newContainer(const Arguments& args) {
    HandleScope scope;

    NewBaton *baton = new NewBaton;
    baton->req.data = baton;

    baton->name = std::string(*String::Utf8Value(args[0]));
    baton->configpath = std::string(*String::Utf8Value(args[1]));

    Local<Function> callback = Local<Function>::Cast(args[2]);
    baton->callback = Persistent<Function>::New(callback);

    uv_queue_work(uv_default_loop(), &baton->req,
            newContainerAsync, newContainerAfter);

    return scope.Close(Undefined());
}

// Methods

// this is just a test, should probably be asynchronous
Handle<Value> state(const Arguments& args) {
    HandleScope scope;

    lxc_container *c = unwrap(args);
    Local<String> state = String::New(c->state(c));

    return scope.Close(state);
}

void init(Handle<Object> exports, Handle<Object> module) {
    HandleScope scope;

    Local<FunctionTemplate>constructorTemplate = FunctionTemplate::New(LXCContainer);
    constructorTemplate->SetClassName(String::NewSymbol("LXCContainer"));
    constructorTemplate->InstanceTemplate()->SetInternalFieldCount(1);

    // Methods
    Local<ObjectTemplate> prototype = constructorTemplate->PrototypeTemplate();

    prototype->Set(String::NewSymbol("state"),
            FunctionTemplate::New(state)->GetFunction());

    constructor = Persistent<Function>::New(constructorTemplate->GetFunction());

    // Exports
    module->Set(String::NewSymbol("exports"),
            FunctionTemplate::New(newContainer)->GetFunction());
}

NODE_MODULE(lxc, init);
