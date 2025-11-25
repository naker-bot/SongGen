#pragma once
#include "CredentialStore.h"
#include <memory>

#ifdef HAVE_LIBSECRET
std::unique_ptr<ICredentialStore> createLibsecretStore();
#endif
