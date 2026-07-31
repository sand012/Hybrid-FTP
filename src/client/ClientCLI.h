#pragma once

#include <functional>
#include <string>


class ClientCLI {
public:
    // Sends one raw command line to the server and returns its reply.
    using CommandSender = std::function<std::string(const std::string& commandLine)>;

    explicit ClientCLI(CommandSender sender);


    void run();

private:
    void printBanner() const;
    void printPrompt() const;
    static std::string trim(const std::string& s);
    static std::string toUpperFirstWord(const std::string& s);

    CommandSender m_sendCommand;
};