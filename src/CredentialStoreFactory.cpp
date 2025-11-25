#include "CredentialStoreFactory.h"
#include <memory>

#ifdef HAVE_LIBSECRET
#include "LibsecretCredentialStore.h"
#endif
#ifdef HAVE_OPENSSL
#include "EncryptedFileCredentialStore.h"
#endif

std::unique_ptr<ICredentialStore> createBestCredentialStore(bool forceEncrypted, const std::string& encryptedFilePath, const std::string& encryptedPassphrase) {
#ifdef HAVE_LIBSECRET
    if (!forceEncrypted) {
        // prefer libsecret if available and not forcing encrypted store
        return createLibsecretStore();
    }
#endif
#ifdef HAVE_OPENSSL
    if (encryptedPassphrase.size() > 0) {
        return createEncryptedFileStore(encryptedFilePath, encryptedPassphrase);
    }
#endif
    // None available
    return nullptr;
}
