#include "net_ctl_codes.h"
#include "net_ctl_obj.h"

Libraries::NetCtl::NetCtlInternal::NetCtlInternal() {
    callbacks.fill({nullptr, nullptr});
    nptoolCallbacks.fill({nullptr, nullptr});
}

Libraries::NetCtl::NetCtlInternal::~NetCtlInternal() {}

s32 Libraries::NetCtl::NetCtlInternal::registerCallback(OrbisNetCtlCallback func, void* arg) {
    std::unique_lock lock{m_mutex};

    // Find the next available slot
    int next_id = 0;
    for (const auto& callback : callbacks) {
        if (callback.func == nullptr) {
            break;
        }
        next_id++;
    }

    if (next_id == 8) {
        return ORBIS_NET_CTL_ERROR_CALLBACK_MAX;
    }

    callbacks[next_id].func = func;
    callbacks[next_id].arg = arg;
    return next_id;
}

s32 Libraries::NetCtl::NetCtlInternal::registerNpToolkitCallback(
    OrbisNetCtlCallbackForNpToolkit func, void* arg) {

    std::unique_lock lock{m_mutex};

    // Find the next available slot
    int next_id = 0;
    for (const auto& callback : nptoolCallbacks) {
        if (callback.func == nullptr) {
            break;
        }
        next_id++;
    }

    if (next_id == 8) {
        return ORBIS_NET_CTL_ERROR_CALLBACK_MAX;
    }

    nptoolCallbacks[next_id].func = func;
    nptoolCallbacks[next_id].arg = arg;
    return next_id;
}

void Libraries::NetCtl::NetCtlInternal::checkCallback() {
    std::unique_lock lock{m_mutex};
    for (auto& callback : callbacks) {
        if (callback.func != nullptr) {
            callback.func(ORBIS_NET_CTL_EVENT_TYPE_DISCONNECTED, callback.arg);
        }
    }
}

void Libraries::NetCtl::NetCtlInternal::checkNpToolkitCallback() {
    std::unique_lock lock{m_mutex};
    for (auto& callback : nptoolCallbacks) {
        if (callback.func != nullptr) {
            callback.func(ORBIS_NET_CTL_EVENT_TYPE_DISCONNECTED, callback.arg);
        }
    }
}
