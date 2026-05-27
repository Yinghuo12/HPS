#include "ddt_auth.h"
#include "ddt_database.h"

#include <algorithm>

namespace ddt {

DDTAuthManager& DDTAuthManager::Instance() {
    static DDTAuthManager inst;
    return inst;
}

bool DDTAuthManager::validateName(const std::string& name) {
    if (name.size() < 2 || name.size() > 16) return false;
    for (char c : name) {
        if (!isalnum((unsigned char)c) && c != '_') return false;
    }
    return true;
}

DDTAuthManager::RegisterResult DDTAuthManager::handleRegister(
        const std::string& name, const std::string& password) {
    RegisterResult result{false, 0, ""};

    if (!validateName(name)) {
        result.msg = "Name must be 2-16 chars (letters, digits, underscore)";
        return result;
    }
    if (password.size() < 4) {
        result.msg = "Password must be at least 4 chars";
        return result;
    }

    auto regResult = DDTDatabase::Instance().registerAccount(name, password);
    if (!regResult.first) {
        result.msg = "Name already exists";
        return result;
    }

    result.ok = true;
    result.accountId = regResult.second;
    result.msg = "OK";
    return result;
}

DDTAuthManager::LoginResult DDTAuthManager::handleLogin(
        const std::string& name, const std::string& password) {
    LoginResult result{false, 0, "", ""};

    auto loginResult = DDTDatabase::Instance().loginAccount(name, password);
    if (!loginResult.first) {
        result.msg = "Invalid name or password";
        return result;
    }

    std::string token = DDTDatabase::Instance().generateToken(loginResult.second);
    DDTDatabase::Instance().setOnline(loginResult.second);

    result.ok = true;
    result.accountId = loginResult.second;
    result.token = token;
    result.msg = "OK";
    return result;
}

void DDTAuthManager::handleLogout(uint64_t accountId) {
    DDTDatabase::Instance().setOffline(accountId);
}

} // namespace ddt
