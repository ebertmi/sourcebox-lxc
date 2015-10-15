#include "config.h"

ConfigWorker::ConfigWorker(lxc_container *container, Nan::Callback *callback,
        const std::string& file, bool save)
        : LxcWorker(container, callback), file_(file), save_(save) {
    requireDefined_ = false;
}

void ConfigWorker::LxcExecute() {
    const char *file = file_.empty() ? nullptr : file_.c_str();

    bool ret;

    if (save_) {
        ret = container_->save_config(container_, file);
    } else {
        ret = container_->load_config(container_, file);
    }

    if (!ret) {
        if (save_) {
            SetErrorMessage("Failed to save config file");
        } else {
            SetErrorMessage("Failed to load config file");
        }
    }
}
