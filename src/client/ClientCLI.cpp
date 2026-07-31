#include "ClientCLI.h"

#include <cctype>
#include <iostream>

ClientCLI::ClientCLI(CommandSender sender) : m_sendCommand(std::move(sender)) {}

void ClientCLI::run() {
    printBanner();

    std::string line;
    while (true) {
        printPrompt();

        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            break;  // EOF (Ctrl+D)
        }

        line = trim(line);
        if (line.empty()) continue;

        std::string reply = m_sendCommand(line);
        std::cout << reply << "\n";

        if (toUpperFirstWord(line) == "QUIT") {
            break;
        }
    }
}

void ClientCLI::printBanner() const {
    std::cout << "Hybrid FTP Client - Day 1-3 skeleton\n";
    std::cout << "Type an FTP command (e.g. USER alice, PASS secret, QUIT).\n";
    std::cout << "Command parsing/validation arrives with Dev 1's Day 4-5 work.\n\n";
}

void ClientCLI::printPrompt() const {
    std::cout << "ftp> " << std::flush;
}

std::string ClientCLI::trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string ClientCLI::toUpperFirstWord(const std::string& s) {
    size_t spacePos = s.find(' ');
    std::string word = (spacePos == std::string::npos) ? s : s.substr(0, spacePos);
    for (char& c : word) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return word;
}