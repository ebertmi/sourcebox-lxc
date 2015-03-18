#include "util.h"

#include <unistd.h>

static bool inAttachedProcess = false;

void exitIfInAttachedProcess(int status, void *) {
    if (inAttachedProcess) {
        _exit(status);
    }
}

int attachWrap(lxc_container *container, lxc_attach_exec_t execFunction,
        void *execPayload, lxc_attach_options_t *options, pid_t *attachedProcess) {
    inAttachedProcess = true;
    int ret = container->attach(container, execFunction, execPayload,
            options, attachedProcess);
    inAttachedProcess = false;
    return ret;
}
