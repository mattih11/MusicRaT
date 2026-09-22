#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

class MusicXMLReader {
public:
    explicit MusicXMLReader(const std::string& filepath) : filepath_(filepath) {}

    std::string read() {
        std::ifstream file(filepath_);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open MusicXML file: " + filepath_);
        }

        return std::string(
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>());
    }

private:
    std::string filepath_;
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <musicxml_file>\n";
        return 1;
    }

    try {
        MusicXMLReader reader(argv[1]);
        const std::string xml = reader.read();
        (void)xml;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }
    return 0;
}