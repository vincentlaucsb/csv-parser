inline std::string data_file(const std::string& filename) {
    char* root;
    root = getenv("CSV_TEST_ROOT");
    if (!root) {
        root = ".";
    }

    return std::string(root) + filename;
}