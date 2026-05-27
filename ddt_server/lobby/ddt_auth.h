#ifndef __DDT_AUTH_H__
#define __DDT_AUTH_H__

#include <string>
#include <cstdint>
#include <utility>

namespace ddt {

class DDTAuthManager {
public:
    static DDTAuthManager& Instance();

    // Returns (ok, account_id, token_or_msg)
    struct RegisterResult {
        bool ok;
        uint64_t accountId;
        std::string msg;
    };

    struct LoginResult {
        bool ok;
        uint64_t accountId;
        std::string token;
        std::string msg;
    };

    RegisterResult handleRegister(const std::string& name, const std::string& password);
    LoginResult handleLogin(const std::string& name, const std::string& password);
    void handleLogout(uint64_t accountId);

    bool validateName(const std::string& name);

private:
    DDTAuthManager() = default;
};

} // namespace ddt

#endif
