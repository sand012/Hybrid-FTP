#include "CommandParser.h"
#include<string>
#include <algorithm> // std::transform
#include <cctype>   

std::string CommandParser::trim(const std::string& text)
{
    const auto first = text.find_first_not_of(" \t\r\n");

    if (first == std::string::npos) {
        return "";
    }

    const auto last = text.find_last_not_of(" \t\r\n");

    return text.substr(first, last - first + 1);
}
std::string CommandParser::toUpper(const std::string& text)
{
    std::string result = text;

    std::transform(
        result.begin(),
        result.end(),
        result.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        }
    );

    return result;
}
ParsedCommand CommandParser::parse(const std::string& input)
{
    ParsedCommand result;

    // Bỏ khoảng trắng và \r\n ở hai đầu
    const std::string cleanedInput = trim(input);

    if (cleanedInput.empty()) {
        return result;
    }

    // Tìm khoảng trắng đầu tiên giữa command và argument
    const std::size_t separator = cleanedInput.find_first_of(" \t");

    if (separator == std::string::npos) {
        // Lệnh không có argument, ví dụ PWD
        result.name = toUpper(cleanedInput);
        result.argument = "";
    }
    else {
        // Lệnh có argument, ví dụ CWD documents
        result.name = toUpper(cleanedInput.substr(0, separator));
        result.argument = trim(cleanedInput.substr(separator + 1));
    }

    result.valid = !result.name.empty();
    return result;
}