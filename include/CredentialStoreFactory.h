#pragma once
#include <memory>
#include <string>
#include "CredentialStore.h"

// create the best available credential store, optionally using encrypted-file fallback
// If forceEncrypted is true and OpenSSL is available, return an EncryptedFileCredentialStore; otherwise prefer libsecret
std::unique_ptr<ICredentialStore> createBestCredentialStore(bool forceEncrypted, const std::string& encryptedFilePath = "", const std::string& encryptedPassphrase = "");
