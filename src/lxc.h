#ifndef SOURCEBOX_LXC_H
#define SOURCEBOX_LXC_H

#include <node.h>
#include <lxc/lxccontainer.h>

v8::Local<v8::Object> Wrap(lxc_container *container);

#endif
