#pragma once
#include "CredentialStore.h"
#include <memory>

#ifdef HAVE_OPENSSL
std::unique_ptr<ICredentialStore> createEncryptedFileStore(const std::string& filePath, const std::string& passphrase);
#endif
