#include <Geode/Geode.hpp>
#include <string>

// Splits a string by a delimiter
std::vector<std::string> splitString(const std::string& str, char delim) {
    std::vector<std::string> tokens;
    std::istringstream stream(str);
    std::string token;
    while (std::getline(stream, token, delim)) {
        tokens.push_back(token); 
    }
    return tokens;
}

// Parses server data
std::vector<int> parseData(std::string const& string) {
	std::vector<std::string> levels = splitString(string, '|');
    std::vector<int> levelIDs;
    // Get the level IDs
    for (int i = 0; i < 10; i++) {
        std::string level = levels[i];
        std::vector<std::string> leveldata = splitString(level, ':');
        try {
            int id = std::stoi(leveldata[1]);
            // Valid conversion
            levelIDs.push_back(id);
        } catch (const std::invalid_argument& e) {
            // Cannot convert to int (USUALLY means that the page has less than 10 levels)
            geode::log::error("Failed to convert to integer: {}", leveldata[1]);
            break;
        }
    }
    return levelIDs;
}