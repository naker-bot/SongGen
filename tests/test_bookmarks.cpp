#include <iostream>
#include <fstream>
#include <filesystem>
#include <unistd.h>
#include "../include/ImGuiRenderer.h"

int main() {
    // Setup isolated HOME
    std::string tmp = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp");
    std::string testHome = tmp + "/songgen_bookmark_test_" + std::to_string(getpid());
    setenv("HOME", testHome.c_str(), 1);
    std::filesystem::path settingsDir = std::filesystem::path(testHome) / ".songgen";
    std::filesystem::create_directories(settingsDir);

    // Ensure defaults
    auto bookmarksFile = settingsDir / "bookmarks.json";
    if (std::filesystem::exists(bookmarksFile)) std::filesystem::remove(bookmarksFile);

    ImGuiRenderer renderer;

    // Create a test local directory
    std::filesystem::path localDir = std::filesystem::path(testHome) / "Music/testdir";
    std::filesystem::create_directories(localDir);

    // Add a local bookmark with trailing slash and test normalization
    std::string localBookmark = localDir.string() + "/";
    renderer.addBookmarkPublic(localBookmark);
    if (!renderer.isBookmarkedPublic(localDir.string())) {
        std::cerr << "Local bookmark normalize failed: isBookmarked not found for path without slash" << std::endl;
        return 2;
    }
    if (!std::filesystem::exists(bookmarksFile)) {
        std::cerr << "bookmarks.json not created" << std::endl;
        return 3;
    }
    // Confirm stored bookmark normalized (no trailing slash)
    std::ifstream in(bookmarksFile);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (content.find(localDir.string()) == std::string::npos) {
        std::cerr << "Normalized local bookmark not present in JSON" << std::endl;
        std::cerr << "Content: " << content << std::endl;
        return 4;
    }

    // Test FTP bookmark normalization (trailing slash removed)
    std::string ftpBook = "ftp://example.com/some/path/";
    renderer.addBookmarkPublic(ftpBook);
    if (!renderer.isBookmarkedPublic("ftp://example.com/some/path")) {
        std::cerr << "FTP bookmark normalize failed" << std::endl;
        return 5;
    }

    // Remove bookmark using variant with trailing slash
    if (!renderer.removeBookmarkPublic(localDir.string() + "/")) {
        std::cerr << "Failed to remove bookmark via path with trailing slash" << std::endl;
        return 6;
    }
    if (renderer.isBookmarkedPublic(localDir.string())) {
        std::cerr << "Bookmark still present after remove" << std::endl;
        return 7;
    }

    std::cout << "Bookmark tests passed." << std::endl;
    return 0;
}
