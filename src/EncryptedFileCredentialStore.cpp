#include "CredentialStore.h"
#include <string>
#include <fstream>
#include <vector>
#include <cstring>
#include <sys/stat.h>
#include <map>
#include <sstream>
#include <iostream>
#include <memory>

#ifdef HAVE_OPENSSL
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#endif

static std::vector<unsigned char> hexToBytes(const std::string& hex) {
    if (hex.empty()) return {};
    std::vector<unsigned char> bytes;
    bytes.reserve(hex.size()/2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        unsigned int byte;
        sscanf(hex.substr(i,2).c_str(), "%02x", &byte);
        bytes.push_back(static_cast<unsigned char>(byte));
    }
    return bytes;
}

#ifdef HAVE_OPENSSL
class EncryptedFileCredentialStore : public ICredentialStore {
public:
    EncryptedFileCredentialStore(const std::string& filePath, const std::string& passphrase) : filePath(filePath), passphrase(passphrase) {}

    bool saveCredentials(const std::string& hostKey, const std::string& username, const std::string& password) override {
        // Load existing map; decrypt, update entry, encrypt back
        std::map<std::string, std::pair<std::string, std::string>> map;
        if (!loadMap(map)) return false;
        map[hostKey] = {username, password};
        return saveMap(map);
    }

    bool loadCredentials(const std::string& hostKey, std::string& username, std::string& password) override {
        std::map<std::string, std::pair<std::string, std::string>> map;
        if (!loadMap(map)) return false;
        auto it = map.find(hostKey);
        if (it == map.end()) return false;
        username = it->second.first;
        password = it->second.second;
        return true;
    }

    bool removeCredentials(const std::string& hostKey) override {
        std::map<std::string, std::pair<std::string, std::string>> map;
        if (!loadMap(map)) return false;
        map.erase(hostKey);
        return saveMap(map);
    }

    bool hasCredentials(const std::string& hostKey) override {
        std::map<std::string, std::pair<std::string, std::string>> map;
        if (!loadMap(map)) return false;
        return map.find(hostKey) != map.end();
    }

private:
    std::string filePath;
    std::string passphrase;

    bool saveMap(const std::map<std::string, std::pair<std::string, std::string>>& map) {
        // Serialize into JSON-like simple format: host\nuser\npass\n---
        std::string plaintext;
        for (auto &kv : map) {
            plaintext += kv.first + "\n" + kv.second.first + "\n" + kv.second.second + "\n";
        }
        // encrypt
        std::vector<unsigned char> salt(16);
        if (!RAND_bytes(salt.data(), salt.size())) return false;
        std::vector<unsigned char> key(32), iv(12);
        if (!deriveKeyFromPassphrase(passphrase, salt, key, iv)) return false;
        std::vector<unsigned char> ciphertext;
        std::vector<unsigned char> tag(16);
        if (!aesGcmEncrypt((unsigned char*)plaintext.data(), plaintext.size(), key, iv, ciphertext, tag)) return false;
        // build file content: magic + salt + iv + tag + ciphertext length + ciphertext
        std::ofstream f(filePath, std::ios::binary);
        if (!f.is_open()) return false;
        f.write("SGCRED1", 7);
        f.write((char*)salt.data(), salt.size());
        f.write((char*)iv.data(), iv.size());
        f.write((char*)tag.data(), tag.size());
        uint64_t ctlen = ciphertext.size();
        f.write((char*)&ctlen, sizeof(ctlen));
        f.write((char*)ciphertext.data(), ciphertext.size());
        f.close();
        chmod(filePath.c_str(), 0600);
        return true;
    }

    bool loadMap(std::map<std::string, std::pair<std::string, std::string>>& map) {
        // If file doesn't exist, return true with empty map
        struct stat st;
        if (stat(filePath.c_str(), &st) != 0) return true; // no file
        std::ifstream f(filePath, std::ios::binary);
        if (!f.is_open()) return false;
        char magic[7] = {0};
        f.read(magic, 7);
        if (std::string(magic) != "SGCRED1") return false;
        std::vector<unsigned char> salt(16), iv(12), tag(16);
        f.read((char*)salt.data(), salt.size());
        f.read((char*)iv.data(), iv.size());
        f.read((char*)tag.data(), tag.size());
        uint64_t ctlen;
        f.read((char*)&ctlen, sizeof(ctlen));
        std::vector<unsigned char> ciphertext(ctlen);
        f.read((char*)ciphertext.data(), ctlen);
        // derive key
        std::vector<unsigned char> key(32);
        if (!deriveKeyFromPassphrase(passphrase, salt, key, iv)) return false;
        std::vector<unsigned char> plaintext;
        if (!aesGcmDecrypt(ciphertext, key, iv, tag, plaintext)) return false;
        // parse plaintext
        std::string data((char*)plaintext.data(), plaintext.size());
        std::istringstream iss(data);
        std::string host, user, pass;
        while (std::getline(iss, host) && std::getline(iss, user) && std::getline(iss, pass)) {
            if (host.size() == 0) continue;
            map[host] = {user, pass};
        }
        return true;
    }

    bool deriveKeyFromPassphrase(const std::string& pass, const std::vector<unsigned char>& salt, std::vector<unsigned char>& outKey, std::vector<unsigned char>& outIv) {
        // PBKDF2-HMAC-SHA256 to derive 44 bytes: 32 key + 12 iv
        const int iterations = 100000;
        std::vector<unsigned char> out(44);
        if (!PKCS5_PBKDF2_HMAC(pass.c_str(), pass.size(), salt.data(), salt.size(), iterations, EVP_sha256(), out.size(), out.data())) return false;
        memcpy(outKey.data(), out.data(), 32);
        memcpy(outIv.data(), out.data()+32, 12);
        return true;
    }

    bool aesGcmEncrypt(unsigned char* plaintext, size_t plaintext_len, const std::vector<unsigned char>& key, const std::vector<unsigned char>& iv, std::vector<unsigned char>& ciphertext, std::vector<unsigned char>& tag) {
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) return false;
        int len;
        int outlen = 0;
        int ciphertext_len = 0;
        ciphertext.resize(plaintext_len);
        if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL)) goto fail;
        if (1 != EVP_EncryptInit_ex(ctx, NULL, NULL, key.data(), iv.data())) goto fail;
        if (1 != EVP_EncryptUpdate(ctx, ciphertext.data(), &outlen, plaintext, plaintext_len)) goto fail;
        ciphertext_len = outlen;
        if (1 != EVP_EncryptFinal_ex(ctx, ciphertext.data() + outlen, &outlen)) goto fail;
        ciphertext_len += outlen;
        ciphertext.resize(ciphertext_len);
        tag.resize(16);
        if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag.data())) goto fail;
        EVP_CIPHER_CTX_free(ctx);
        return true;
    fail:
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    bool aesGcmDecrypt(const std::vector<unsigned char>& ciphertext, const std::vector<unsigned char>& key, const std::vector<unsigned char>& iv, const std::vector<unsigned char>& tag, std::vector<unsigned char>& plaintext) {
        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) return false;
        int len;
        int outlen = 0;
        int plaintext_len = 0;
        plaintext.resize(ciphertext.size());
        if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL)) goto fail;
        if (1 != EVP_DecryptInit_ex(ctx, NULL, NULL, key.data(), iv.data())) goto fail;
        if (1 != EVP_DecryptUpdate(ctx, plaintext.data(), &outlen, ciphertext.data(), ciphertext.size())) goto fail;
        plaintext_len = outlen;
        if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, tag.size(), (void*)tag.data())) goto fail;
        if (1 != EVP_DecryptFinal_ex(ctx, plaintext.data() + outlen, &outlen)) goto fail;
        plaintext_len += outlen;
        plaintext.resize(plaintext_len);
        EVP_CIPHER_CTX_free(ctx);
        return true;
    fail:
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }
};
#endif // HAVE_OPENSSL

#ifdef HAVE_OPENSSL
std::unique_ptr<ICredentialStore> createEncryptedFileStore(const std::string& filePath, const std::string& passphrase) {
    return std::make_unique<EncryptedFileCredentialStore>(filePath, passphrase);
}
#endif
