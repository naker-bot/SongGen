#include "CredentialStore.h"
#include <string>
#ifdef HAVE_LIBSECRET
#include <libsecret/secret.h>
#include <glib.h>
#include <memory>

static const SecretSchema SONGGEN_FTP_SCHEMA = {
    "org.songgen.ftp", SECRET_SCHEMA_NONE,
    {
        { "host", SECRET_SCHEMA_ATTRIBUTE_STRING },
        { NULL, SECRET_SCHEMA_ATTRIBUTE_STRING }
    }
};

class LibsecretCredentialStore : public ICredentialStore {
public:
    bool saveCredentials(const std::string& hostKey, const std::string& username, const std::string& password) override;
    bool loadCredentials(const std::string& hostKey, std::string& username, std::string& password) override;
    bool removeCredentials(const std::string& hostKey) override;
    bool hasCredentials(const std::string& hostKey) override;
};

bool LibsecretCredentialStore::saveCredentials(const std::string& hostKey, const std::string& username, const std::string& password) {
    std::string secret = username + ":" + password;
    GError* error = nullptr;
    gboolean ok = secret_password_store_sync(&SONGGEN_FTP_SCHEMA, SECRET_COLLECTION_DEFAULT, "SongGen FTP", secret.c_str(), NULL, &error, "host", hostKey.c_str(), NULL);
    if (!ok) {
        if (error) {
            g_warning("secret store error: %s", error->message);
            g_error_free(error);
        }
        return false;
    }
    return true;
}

bool LibsecretCredentialStore::loadCredentials(const std::string& hostKey, std::string& username, std::string& password) {
    GError* error = nullptr;
    char* result = secret_password_lookup_sync(&SONGGEN_FTP_SCHEMA, NULL, &error, "host", hostKey.c_str(), NULL);
    if (!result) {
        if (error) g_error_free(error);
        return false;
    }
    std::string creds(result);
    g_free(result);
    auto p = creds.find(':');
    if (p == std::string::npos) return false;
    username = creds.substr(0, p);
    password = creds.substr(p + 1);
    return true;
}

bool LibsecretCredentialStore::removeCredentials(const std::string& hostKey) {
    GError* error = nullptr;
    gboolean ok = secret_password_clear_sync(&SONGGEN_FTP_SCHEMA, NULL, &error, "host", hostKey.c_str(), NULL);
    if (!ok) {
        if (error) g_error_free(error);
        return false;
    }
    return true;
}

bool LibsecretCredentialStore::hasCredentials(const std::string& hostKey) {
    std::string u, p;
    return loadCredentials(hostKey, u, p);
}

#endif // HAVE_LIBSECRET

#ifdef HAVE_LIBSECRET
std::unique_ptr<ICredentialStore> createLibsecretStore() {
    return std::make_unique<LibsecretCredentialStore>();
}
#endif
