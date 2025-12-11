#include<filesystem>
#include<vector>


class customExceptions : public std::exception {
private:
    std::string message;

public:
    customExceptions(const std::string& msg); 
    const char* what() const noexcept override;
};


struct FileTreeNode {
    std::string name = "";
    std::string fullPath = "";
    bool isDirectory = false;
    size_t fileSize = 0;
    int depth = 0;
    
    FileTreeNode* parent = nullptr;
    std::vector<FileTreeNode*> children;
    
    FileTreeNode();
    FileTreeNode(const std::string& name, const std::string& path, bool isDir);
    
};

void displayFileTree(FileTreeNode directory);

int returnCountOf(std::string filename, std::string keyWord, char breakOperand);

