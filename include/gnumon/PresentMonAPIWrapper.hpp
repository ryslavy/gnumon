#pragma once

#include "PresentMonAPI.h"
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <optional>
#include <cstdint>

namespace pmapi {

class PmException : public std::runtime_error {
public:
    explicit PmException(PM_STATUS status, const std::string& message = "")
        : std::runtime_error(message.empty() ? ("PresentMon API error code: " + std::to_string(status)) : message),
          status_(status) {}

    PM_STATUS GetStatus() const noexcept { return status_; }

private:
    PM_STATUS status_;
};

inline void CheckStatus(PM_STATUS status, const std::string& msg = "") {
    if (status != PM_STATUS_SUCCESS) {
        throw PmException(status, msg);
    }
}

class Session;

class FrameQuery {
public:
    FrameQuery(PM_SESSION_HANDLE session, const std::vector<PM_QUERY_ELEMENT>& elements) {
        std::vector<PM_QUERY_ELEMENT> copy = elements;
        CheckStatus(pmRegisterFrameQuery(session, &handle_, copy.data(), copy.size(), &blobSize_),
                    "Failed to register frame query");
    }

    ~FrameQuery() {
        if (handle_) {
            pmFreeFrameQuery(handle_);
            handle_ = nullptr;
        }
    }

    FrameQuery(const FrameQuery&) = delete;
    FrameQuery& operator=(const FrameQuery&) = delete;

    FrameQuery(FrameQuery&& other) noexcept : handle_(other.handle_), blobSize_(other.blobSize_) {
        other.handle_ = nullptr;
        other.blobSize_ = 0;
    }

    FrameQuery& operator=(FrameQuery&& other) noexcept {
        if (this != &other) {
            if (handle_) pmFreeFrameQuery(handle_);
            handle_ = other.handle_;
            blobSize_ = other.blobSize_;
            other.handle_ = nullptr;
            other.blobSize_ = 0;
        }
        return *this;
    }

    uint32_t GetBlobSize() const noexcept { return blobSize_; }
    PM_FRAME_QUERY_HANDLE GetHandle() const noexcept { return handle_; }

    uint32_t ConsumeFrames(uint32_t processId, uint8_t* pBlobBuffer, uint32_t maxFrames) {
        uint32_t framesRead = maxFrames;
        CheckStatus(pmConsumeFrames(handle_, processId, pBlobBuffer, &framesRead),
                    "Failed to consume frames from query");
        return framesRead;
    }

private:
    PM_FRAME_QUERY_HANDLE handle_ = nullptr;
    uint32_t blobSize_ = 0;
};

class DynamicQuery {
public:
    DynamicQuery(PM_SESSION_HANDLE session, const std::vector<PM_QUERY_ELEMENT>& elements, double windowSizeMs = 1000.0, double offsetMs = 0.0) {
        std::vector<PM_QUERY_ELEMENT> copy = elements;
        CheckStatus(pmRegisterDynamicQuery(session, &handle_, copy.data(), copy.size(), windowSizeMs, offsetMs),
                    "Failed to register dynamic query");
    }

    ~DynamicQuery() {
        if (handle_) {
            pmFreeDynamicQuery(handle_);
            handle_ = nullptr;
        }
    }

    DynamicQuery(const DynamicQuery&) = delete;
    DynamicQuery& operator=(const DynamicQuery&) = delete;

    DynamicQuery(DynamicQuery&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    DynamicQuery& operator=(DynamicQuery&& other) noexcept {
        if (this != &other) {
            if (handle_) pmFreeDynamicQuery(handle_);
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    PM_DYNAMIC_QUERY_HANDLE GetHandle() const noexcept { return handle_; }

    void Poll(uint32_t processId, uint8_t* pBlob, uint32_t* pNumSwapChains = nullptr) {
        uint32_t dummy = 0;
        CheckStatus(pmPollDynamicQuery(handle_, processId, pBlob, pNumSwapChains ? pNumSwapChains : &dummy),
                    "Failed to poll dynamic query");
    }

private:
    PM_DYNAMIC_QUERY_HANDLE handle_ = nullptr;
};

class IntrospectionRoot {
public:
    explicit IntrospectionRoot(const PM_INTROSPECTION_ROOT* pRoot) : pRoot_(pRoot) {}

    ~IntrospectionRoot() {
        if (pRoot_) {
            pmFreeIntrospectionRoot(pRoot_);
            pRoot_ = nullptr;
        }
    }

    IntrospectionRoot(const IntrospectionRoot&) = delete;
    IntrospectionRoot& operator=(const IntrospectionRoot&) = delete;

    IntrospectionRoot(IntrospectionRoot&& other) noexcept : pRoot_(other.pRoot_) {
        other.pRoot_ = nullptr;
    }

    IntrospectionRoot& operator=(IntrospectionRoot&& other) noexcept {
        if (this != &other) {
            if (pRoot_) pmFreeIntrospectionRoot(pRoot_);
            pRoot_ = other.pRoot_;
            other.pRoot_ = nullptr;
        }
        return *this;
    }

    const PM_INTROSPECTION_ROOT* GetRaw() const noexcept { return pRoot_; }
    const PM_INTROSPECTION_ROOT* operator->() const noexcept { return pRoot_; }

private:
    const PM_INTROSPECTION_ROOT* pRoot_ = nullptr;
};

class Session {
public:
    Session() {
        CheckStatus(pmOpenSession(&handle_), "Failed to open PresentMon session");
    }

    explicit Session(const std::string& pipeName) {
        CheckStatus(pmOpenSessionWithPipe(&handle_, pipeName.c_str()), "Failed to open PresentMon session with pipe");
    }

    ~Session() {
        if (handle_) {
            pmCloseSession(handle_);
            handle_ = nullptr;
        }
    }

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    Session(Session&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    Session& operator=(Session&& other) noexcept {
        if (this != &other) {
            if (handle_) pmCloseSession(handle_);
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    PM_SESSION_HANDLE GetHandle() const noexcept { return handle_; }

    void StartTrackingProcess(uint32_t processId) {
        CheckStatus(pmStartTrackingProcess(handle_, processId), "Failed to start tracking process");
    }

    void StopTrackingProcess(uint32_t processId) {
        CheckStatus(pmStopTrackingProcess(handle_, processId), "Failed to stop tracking process");
    }

    DynamicQuery RegisterDynamicQuery(const std::vector<PM_QUERY_ELEMENT>& elements, double windowSizeMs = 1000.0, double offsetMs = 0.0) {
        return DynamicQuery(handle_, elements, windowSizeMs, offsetMs);
    }

    FrameQuery RegisterFrameQuery(const std::vector<PM_QUERY_ELEMENT>& elements) {
        return FrameQuery(handle_, elements);
    }

    IntrospectionRoot GetIntrospectionRoot() {
        const PM_INTROSPECTION_ROOT* pRoot = nullptr;
        CheckStatus(pmGetIntrospectionRoot(handle_, &pRoot), "Failed to retrieve introspection root");
        return IntrospectionRoot(pRoot);
    }

    template<typename T>
    T PollStatic(PM_METRIC metric, uint32_t deviceId = 0, uint32_t processId = 0) {
        T value{};
        PM_QUERY_ELEMENT elem{ metric, PM_STAT_NONE, deviceId, 0, 0, sizeof(T) };
        CheckStatus(pmPollStaticQuery(handle_, &elem, processId, reinterpret_cast<uint8_t*>(&value)),
                    "Failed to poll static query");
        return value;
    }

    std::string PollStaticString(PM_METRIC metric, uint32_t deviceId = 0, uint32_t processId = 0) {
        char buf[256]{};
        PM_QUERY_ELEMENT elem{ metric, PM_STAT_NONE, deviceId, 0, 0, sizeof(buf) };
        CheckStatus(pmPollStaticQuery(handle_, &elem, processId, reinterpret_cast<uint8_t*>(buf)),
                    "Failed to poll static string query");
        return std::string(buf);
    }

    static PM_VERSION GetApiVersion() {
        PM_VERSION ver{};
        CheckStatus(pmGetApiVersion(&ver), "Failed to query API version");
        return ver;
    }

private:
    PM_SESSION_HANDLE handle_ = nullptr;
};

} // namespace pmapi
