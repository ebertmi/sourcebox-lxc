#ifndef SOURCEBOX_UTIL_H
#define SOURCEBOX_UTIL_H

#include <lxc/lxccontainer.h>

void exitIfInAttachedProcess(int status, void *);

int attachWrap(lxc_container *container, lxc_attach_exec_t execFunction,
        void *execPayload, lxc_attach_options_t *options, pid_t *attachedProcess);

#endif
