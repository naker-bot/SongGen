#include "include/SonglengthsDB.h"
#include <iostream>

int main() {
    std::string testSid = "/home/nex/.songgen/hvsc/C64Music/DEMOS/0-9/10_Orbyte.sid";
    std::string md5 = SonglengthsDB::calculateSidMD5(testSid);
    std::cout << "SID: " << testSid << std::endl;
    std::cout << "MD5: " << md5 << std::endl;
    std::cout << "Expected: 5f08a730b280e54fd1e75a7046b93fdc" << std::endl;
    return 0;
}
