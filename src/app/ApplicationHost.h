#pragma once

#include <memory>
#include <string>

namespace flickvr::app {

class ApplicationHost final {
public:
    ApplicationHost();
    ~ApplicationHost();

    ApplicationHost(const ApplicationHost&) = delete;
    ApplicationHost& operator=(const ApplicationHost&) = delete;

    [[nodiscard]] bool Initialize(std::string* error = nullptr);
    [[nodiscard]] int Run();
    void Shutdown() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace flickvr::app
