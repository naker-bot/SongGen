#include "include/SonglengthsDB.h"
#include <iostream>

int main() {
    SonglengthsDB db;
    std::string dbPath = "/home/nex/.songgen/hvsc/C64Music/DOCUMENTS/Songlengths.md5";
    
    if (db.load(dbPath)) {
        std::string testSid = "/home/nex/.songgen/hvsc/C64Music/DEMOS/0-9/10_Orbyte.sid";
        int length = db.getLength(testSid, 1);
        std::cout << "SID: " << testSid << std::endl;
        std::cout << "Länge aus DB: " << length << " Sekunden" << std::endl;
        std::cout << "Erwartet: 77 Sekunden (1:17)" << std::endl;
        std::cout << std::endl;
        
        testSid = "/home/nex/.songgen/hvsc/C64Music/MUSICIANS/C/Cooksey_Mark/1942.sid";
        length = db.getLength(testSid, 2);
        std::cout << "SID: 1942.sid Subtune 2" << std::endl;
        std::cout << "Länge aus DB: " << length << " Sekunden" << std::endl;
        std::cout << "Erwartet: 421 Sekunden (7:01)" << std::endl;
    }
    
    return 0;
}
