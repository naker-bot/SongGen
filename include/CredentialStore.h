#pragma once
#include <string>

class ICredentialStore {
public:
    virtual ~ICredentialStore() {}
    virtual bool saveCredentials(const std::string& hostKey, const std::string& username, const std::string& password) = 0;
    virtual bool loadCredentials(const std::string& hostKey, std::string& username, std::string& password) = 0;
    virtual bool removeCredentials(const std::string& hostKey) = 0;
    virtual bool hasCredentials(const std::string& hostKey) = 0;
};
