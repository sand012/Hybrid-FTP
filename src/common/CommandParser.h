#pragma once

#include <string>

// Kết quả sau khi phân tích một dòng lệnh FTP
struct ParsedCommand {
    std::string name;       // Tên lệnh: USER, PASS, PWD, CWD...
    std::string argument;   // Tham số phía sau lệnh
    bool valid = false;     // Lệnh có phân tích được hay không
};

class CommandParser {
public:
    // Ví dụ: "CWD documents\r\n"
    // name = "CWD", argument = "documents"
    static ParsedCommand parse(const std::string& input);

private:
    static std::string trim(const std::string& text);
    static std::string toUpper(const std::string& text);
};