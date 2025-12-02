#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>
#include <filesystem>
#include <windows.h>
#include <algorithm>     // Used for std::sort and std::remove_if
#include <openssl/sha.h> // Used for SHA-1 hashing
#include <zlib.h>        // Used for compression/decompression

using namespace std;
namespace fs = std::filesystem;

const string REPO_DIR = ".mygit";
const int BUFFER_SIZE = 8192; // Not directly used, but good practice

// Utility function to extract the filename from a full or relative path
string getFilename(const string &path)
{
    size_t pos = path.find_last_of("\\/");
    if (pos != string::npos)
    {
        return path.substr(pos + 1);
    }
    return path;
}

// ============= UTILITY FUNCTIONS =============

// Computes the SHA-1 hash of the given data string
string computeSHA1(const string &data)
{
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char *>(data.c_str()), data.size(), hash);

    stringstream ss;
    // Format the hash as a 40-character hexadecimal string
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
    {
        ss << hex << setw(2) << setfill('0') << (int)hash[i];
    }
    return ss.str();
}

// Compresses data using the zlib library
string compressData(const string &data)
{
    uLongf compressedSize = compressBound(data.size()); // Estimate max compressed size
    string compressed(compressedSize, '\0');

    if (compress(reinterpret_cast<Bytef *>(&compressed[0]), &compressedSize,
                 reinterpret_cast<const Bytef *>(data.c_str()), data.size()) != Z_OK)
    {
        cerr << "Error: Compression failed" << endl;
        return "";
    }

    compressed.resize(compressedSize); // Truncate to actual size
    return compressed;
}

// Decompresses data using the zlib library with dynamic buffer resizing
string decompressData(const string &compressed)
{
    uLongf uncompressedSize = compressed.size() * 10; // Initial size guess
    string uncompressed;

    while (true)
    {
        uncompressed.resize(uncompressedSize);
        // Attempt decompression
        int result = uncompress(reinterpret_cast<Bytef *>(&uncompressed[0]), &uncompressedSize,
                                reinterpret_cast<const Bytef *>(compressed.c_str()), compressed.size());

        if (result == Z_OK)
        {
            uncompressed.resize(uncompressedSize); // Truncate to actual size
            return uncompressed;
        }
        else if (result == Z_BUF_ERROR)
        {
            uncompressedSize *= 2; // Double the buffer size and try again
        }
        else
        {
            cerr << "Error: Decompression failed" << endl;
            return "";
        }
    }
}

// Standard file system utilities
bool fileExists(const string &path)
{
    return fs::exists(path);
}

bool isDirectory(const string &path)
{
    return fs::is_directory(path);
}

void createDirectories(const string &path)
{
    fs::create_directories(path);
}

// Reads the entire content of a file into a string (binary mode)
string readFile(const string &path)
{
    if (!fs::exists(path))
    {
        cerr << "Error: Cannot read file " << path << endl;
        return "";
    }
    ifstream file(path, ios::binary);
    // Efficiently reads the entire file content using stream iterators
    return string((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
}

// Writes data to a file, creating parent directories if necessary (binary mode)
void writeFile(const string &path, const string &data)
{
    fs::create_directories(fs::path(path).parent_path());
    ofstream file(path, ios::binary);
    if (!file)
    {
        cerr << "Error: Cannot write file " << path << endl;
        return;
    }
    file << data;
}

// Determines the file mode (permissions) for Tree entries (040000 for tree, 100755 for executable, 100644 otherwise)
string getPermissions(const string &path)
{
    if (!fs::exists(path))
        return "100644";
    return fs::is_directory(path) ? "040000" : ((fs::status(path).permissions() & fs::perms::owner_exec) != fs::perms::none) ? "100755"
                                                                                                                             : "100644";
}

// ============= OBJECT STORAGE =============

// Returns the full path where the object with the given SHA is stored (e.g., .mygit/objects/aa/bbbb...)
string getObjectPath(const string &sha)
{
    return REPO_DIR + "\\objects\\" + sha.substr(0, 2) + "\\" + sha.substr(2);
}

// Compresses and writes the raw object content (header + data) to the object database
void writeObject(const string &sha, const string &content)
{
    string path = getObjectPath(sha);
    string compressed = compressData(content);
    if (!compressed.empty())
    {
        writeFile(path, compressed);
    }
}

// Reads, decompresses, and returns the raw object content from the object database
string readObject(const string &sha)
{
    string path = getObjectPath(sha);
    if (!fileExists(path))
    {
        cerr << "Error: Object " << sha << " not found" << endl;
        return "";
    }
    string compressed = readFile(path);
    return decompressData(compressed);
}

// ============= BLOB OPERATIONS =============

// Creates a Blob object for the file content
string createBlob(const string &filepath)
{
    string content = readFile(filepath);
    if (content.empty() && !fileExists(filepath))
        return "";

    // Blob object format: "blob <size>\0<content>"
    string blobData = "blob " + to_string(content.size()) + string(1, '\0') + content;
    string sha = computeSHA1(blobData);

    writeObject(sha, blobData);
    return sha;
}

// ============= TREE OPERATIONS =============

// Structure to hold one entry (file or directory) within a Tree object
struct TreeEntry
{
    string mode;
    string name;
    string sha;
    bool isTree;
};

// Forward declaration for recursion
string createTree(const string &path);

// Recursively scans a directory, creates Blobs/Trees for its contents, and returns a list of TreeEntry structs
vector<TreeEntry> listDirectory(const string &path)
{
    vector<TreeEntry> entries;
    for (const auto &entry : fs::directory_iterator(path))
    {
        string name = entry.path().filename().string();
        // Skip . (current), .. (parent), and the repository directory (.mygit)
        if (name == "." || name == ".." || name == REPO_DIR)
            continue;

        string fullPath = (path == ".") ? name : path + "\\" + name;
        TreeEntry te;
        te.name = name;
        te.mode = getPermissions(fullPath);
        te.isTree = isDirectory(fullPath);

        if (te.isTree)
        {
            // Recursion: If it's a directory, create a Tree object for it
            te.sha = createTree(fullPath);
        }
        else
        {
            // If it's a file, create a Blob object for it
            te.sha = createBlob(fullPath);
        }

        if (!te.sha.empty())
        {
            entries.push_back(te);
        }
    }

    // Sort entries by name (required for consistent Tree SHA-1 hash)
    sort(entries.begin(), entries.end(),
         [](const TreeEntry &a, const TreeEntry &b)
         { return a.name < b.name; });

    return entries;
}

// Creates a Tree object from the directory contents found by listDirectory
string createTree(const string &path)
{
    vector<TreeEntry> entries = listDirectory(path);

    stringstream treeContent;
    // Format: mode name\0sha-1 (repeated)
    for (const auto &entry : entries)
    {
        treeContent << entry.mode << " " << entry.name << '\0' << entry.sha;
    }

    // Tree object format: "tree <size>\0<content>"
    string treeData = "tree " + to_string(treeContent.str().size()) + '\0' + treeContent.str();
    string sha = computeSHA1(treeData);

    writeObject(sha, treeData); // Write the new Tree object to the database
    return sha;
}

// Parses a Tree object's content into a vector of TreeEntry structs
vector<TreeEntry> parseTree(const string &treeSha)
{
    vector<TreeEntry> entries;
    string treeData = readObject(treeSha);
    if (treeData.empty())
        return entries;

    size_t nullPos = treeData.find('\0');
    if (nullPos == string::npos)
        return entries;

    // Skip the "tree <size>" header
    size_t pos = nullPos + 1;

    while (pos < treeData.size())
    {
        size_t spacePos = treeData.find(' ', pos);
        if (spacePos == string::npos)
            break;

        TreeEntry entry;
        entry.mode = treeData.substr(pos, spacePos - pos);
        pos = spacePos + 1;

        size_t nullPos2 = treeData.find('\0', pos);
        if (nullPos2 == string::npos)
            break;

        entry.name = treeData.substr(pos, nullPos2 - pos);
        pos = nullPos2 + 1;

        if (pos + 40 > treeData.size())
            break;
        entry.sha = treeData.substr(pos, 40);
        pos += 40;

        entry.isTree = (entry.mode == "040000");
        entries.push_back(entry);
    }

    return entries;
}

// ============= COMMIT OPERATIONS =============

// Creates a Commit object
string createCommit(const string &treeSha, const string &parentSha, const string &message)
{
    time_t now = time(nullptr);
    // string timestamp = ctime(&now);
    // timestamp.pop_back();

    stringstream commitContent;
    commitContent << "tree " << treeSha << "\n";
    if (!parentSha.empty() && parentSha != "0000000000000000000000000000000000000000")
    {
        commitContent << "parent " << parentSha << "\n";
    }
    commitContent << "author User <user@example.com> " << now << " +0000\n";
    commitContent << "committer User <user@example.com> " << now << " +0000\n";
    commitContent << "\n"
                  << message << "\n";

    // Commit object format: "commit <size>\0<content>"
    string commitData = "commit " + to_string(commitContent.str().size()) + '\0' + commitContent.str();
    string sha = computeSHA1(commitData);

    writeObject(sha, commitData);
    return sha;
}

struct CommitInfo
{
    string treeSha;
    string parentSha;
    string author;
    string committer;
    string message;
    string timestamp;
};

// Parses a Commit object's content into a CommitInfo struct
CommitInfo parseCommit(const string &commitSha)
{
    CommitInfo info;
    string commitData = readObject(commitSha);
    // ... (parsing logic) ...
    if (commitData.empty())
        return info;

    size_t nullPos = commitData.find('\0');
    if (nullPos == string::npos)
        return info;

    string content = commitData.substr(nullPos + 1);
    istringstream stream(content);
    string line;

    while (getline(stream, line))
    {
        if (line.empty())
            break;

        if (line.substr(0, 5) == "tree ")
        {
            info.treeSha = line.substr(5);
        }
        else if (line.substr(0, 7) == "parent ")
        {
            info.parentSha = line.substr(7);
        }
        else if (line.substr(0, 7) == "author ")
        {
            info.author = line.substr(7);
        }
        else if (line.substr(0, 10) == "committer ")
        {
            info.committer = line.substr(10);
            size_t timePos = info.committer.rfind(' ');
            if (timePos != string::npos)
            {
                info.timestamp = info.committer.substr(timePos + 1);
            }
        }
    }

    // Read commit message (everything after the blank line)
    while (getline(stream, line))
    {
        info.message += line + "\n";
    }

    return info;
}

// ============= INDEX OPERATIONS =============

// Structure representing an entry in the staging area (index file)
struct IndexEntry
{
    string path;
    string sha;
    string mode;
};

// Reads the contents of the plaintext index file into a vector of IndexEntry structs
vector<IndexEntry> readIndex()
{
    vector<IndexEntry> entries;
    string indexPath = REPO_DIR + "\\index";
    // ... (reading logic) ...
    if (!fileExists(indexPath))
        return entries;

    ifstream file(indexPath);
    string line;
    while (getline(file, line))
    {
        istringstream iss(line);
        IndexEntry entry;
        iss >> entry.mode >> entry.sha >> entry.path;
        entries.push_back(entry);
    }
    return entries;
}

// Writes the vector of IndexEntry structs back to the plaintext index file
void writeIndex(const vector<IndexEntry> &entries)
{
    string indexPath = REPO_DIR + "\\index";
    ofstream file(indexPath);
    for (const auto &entry : entries)
    {
        file << entry.mode << " " << entry.sha << " " << entry.path << "\n";
    }
}

// Adds a file or directory to the staging area (index)
void addToIndex(const string &path)
{
    if (!fileExists(path))
    {
        cerr << "Error: File " << path << " does not exist" << endl;
        return;
    }

    vector<IndexEntry> index = readIndex();

    if (isDirectory(path))
    {
        // Recursively add all files in a directory
        for (const auto &entry : fs::directory_iterator(path))
        {
            string name = entry.path().filename().string();
            if (name == "." || name == ".." || name == REPO_DIR)
                continue;

            string fullPath = (path == ".") ? name : path + "\\" + name;
            addToIndex(fullPath);
        }
        return;
    }

    // Create blob and add to index
    string sha = createBlob(path);
    if (sha.empty())
        return;

    // Remove existing entry for this path (if updating the file)
    index.erase(remove_if(index.begin(), index.end(),
                          [&path](const IndexEntry &e)
                          { return e.path == path; }),
                index.end());

    // Add new entry
    IndexEntry newEntry;
    newEntry.path = path;
    newEntry.sha = sha;
    newEntry.mode = getPermissions(path);
    index.push_back(newEntry);

    writeIndex(index);
}

// Creates a Tree object from the staged files in the index (used by 'commit')
string createTreeFromIndex()
{
    vector<IndexEntry> index = readIndex();
    if (index.empty())
        return "";

    stringstream treeContent;
    for (const auto &entry : index)
    {
        // Uses just filename, not full path, for the Tree entry name
        string filename = getFilename(entry.path);
        treeContent << entry.mode << " " << filename << '\0' << entry.sha;
    }

    string treeData = "tree " + to_string(treeContent.str().size()) + '\0' + treeContent.str();
    string sha = computeSHA1(treeData);

    writeObject(sha, treeData);
    return sha;
}

// ============= REFERENCE OPERATIONS =============

// Retrieves the SHA of the commit currently pointed to by HEAD (e.g., the commit at refs/heads/master)
string getHEAD()
{
    string headPath = REPO_DIR + "\\HEAD";
    if (!fileExists(headPath))
        return "";

    string content = readFile(headPath);
    if (content.substr(0, 5) == "ref: ")
    {
        string refPath = REPO_DIR + "\\" + content.substr(5);
        if (refPath.back() == '\n')
            refPath.pop_back();
        if (fileExists(refPath))
        {
            string sha = readFile(refPath);
            if (sha.back() == '\n')
                sha.pop_back();
            return sha;
        }
    }
    return "";
}

// Updates the reference pointed to by HEAD (e.g., writes new commit SHA to refs/heads/master)
void updateHEAD(const string &commitSha)
{
    string headPath = REPO_DIR + "\\HEAD";
    string content = readFile(headPath);

    if (content.substr(0, 5) == "ref: ")
    {
        string refPath = content.substr(5);
        if (refPath.back() == '\n')
            refPath.pop_back();
        writeFile(REPO_DIR + "\\" + refPath, commitSha + "\n");
    }
}

// Appends commit details to the log file (.mygit/logs/HEAD)
void updateLog(const string &commitSha, const string &parentSha, const string &message)
{
    string logPath = REPO_DIR + "\\logs\\HEAD";
    ofstream file(logPath, ios::app);
    time_t now = time(nullptr);
    file << "commit " << commitSha << "\n";
    if (!parentSha.empty())
    {
        file << "parent " << parentSha << "\n";
    }
    file << "message " << message << "\n";
    file << "timestamp " << now << "\n";
    file << "---\n";
}

// Recursively restores the working directory based on the contents of a Tree object
void restoreTree(const string &treeSha, const string &prefix = "")
{
    vector<TreeEntry> entries = parseTree(treeSha);

    for (const auto &entry : entries)
    {
        if (entry.name.empty())
        {
            continue;
        }

        string fullPath = prefix.empty() ? entry.name : prefix + "\\" + entry.name;

        if (entry.isTree)
        {
            try
            {
                // Create directory and recurse into it
                if (!fs::exists(fullPath))
                {
                    fs::create_directories(fullPath);
                }
                restoreTree(entry.sha, fullPath);
            }
            catch (const fs::filesystem_error &e)
            {
                cerr << "Error creating directory " << fullPath << ": " << e.what() << endl;
            }
        }
        else
        {
            try
            {
                // Read Blob content and write to file
                string blobData = readObject(entry.sha);
                if (blobData.empty())
                    continue;

                size_t nullPos = blobData.find('\0');
                if (nullPos == string::npos)
                    continue;

                string content = blobData.substr(nullPos + 1);

                fs::path filePath(fullPath);
                if (filePath.has_parent_path() && !filePath.parent_path().empty())
                {
                    fs::create_directories(filePath.parent_path());
                }

                ofstream outFile(fullPath, ios::binary);
                if (outFile)
                {
                    outFile << content;
                    outFile.close();
                }
            }
            catch (const exception &e)
            {
                cerr << "Error restoring file " << fullPath << ": " << e.what() << endl;
            }
        }
    }
}

// ============= COMMAND IMPLEMENTATIONS =============

// Initializes the repository structure
void cmdInit()
{
    if (fileExists(REPO_DIR))
    {
        cout << "Repository already initialized" << endl;
        return;
    }

    fs::create_directories(REPO_DIR + "\\objects");
    fs::create_directories(REPO_DIR + "\\refs");
    fs::create_directories(REPO_DIR + "\\refs\\heads");
    fs::create_directories(REPO_DIR + "\\logs");

    writeFile(REPO_DIR + "\\HEAD", "ref: refs\\heads\\master\n");
    writeFile(REPO_DIR + "\\index", "");

    cout << "Initialized empty repository in " << REPO_DIR << endl;
}

// Computes SHA-1 hash and optionally writes a Blob
void cmdHashObject(const string &filepath, bool write)
{
    string sha = createBlob(filepath);
    if (!sha.empty())
    {
        cout << sha << endl;
    }
}

// Displays object content or metadata
void cmdCatFile(const string &sha, char flag)
{
    string data = readObject(sha);
    if (data.empty())
        return;

    size_t nullPos = data.find('\0');
    if (nullPos == string::npos)
        return;

    string header = data.substr(0, nullPos);
    string content = data.substr(nullPos + 1);

    size_t spacePos = header.find(' ');
    string type = header.substr(0, spacePos);
    string sizeStr = header.substr(spacePos + 1);

    if (flag == 'p') // Print content
    {
        cout << content;
    }
    else if (flag == 's') // Print size
    {
        cout << sizeStr << endl;
    }
    else if (flag == 't') // Print type
    {
        cout << type << endl;
    }
}

// Creates a Tree object from the current working directory's state
void cmdWriteTree()
{
    string treeSha = createTree(".");
    cout << treeSha << endl;
}

// Lists the contents of a Tree object
void cmdLsTree(const string &treeSha, bool nameOnly)
{
    vector<TreeEntry> entries = parseTree(treeSha);

    for (const auto &entry : entries)
    {
        if (nameOnly)
        {
            cout << entry.name << endl;
        }
        else
        {
            string type = entry.isTree ? "tree" : "blob";
            cout << entry.mode << " " << type << " " << entry.sha << "\t" << entry.name << endl;
        }
    }
}

// Adds paths to the staging area (index)
void cmdAdd(const vector<string> &paths)
{
    for (const auto &path : paths)
    {
        addToIndex(path);
    }
}

// Creates a new commit
void cmdCommit(const string &message)
{
    string treeSha = createTreeFromIndex(); // Create Tree from staged files
    if (treeSha.empty())
    {
        cout << "Nothing to commit" << endl;
        return;
    }

    string parentSha = getHEAD();
    string commitSha = createCommit(treeSha, parentSha, message);

    updateHEAD(commitSha);
    updateLog(commitSha, parentSha, message);

    // Clear index after a successful commit (optional but common practice)
    writeFile(REPO_DIR + "\\index", "");

    cout << commitSha << endl;
}

// Displays commit history from the log file
void cmdLog()
{
    string logPath = REPO_DIR + "\\logs\\HEAD";
    if (!fileExists(logPath))
    {
        cout << "No commits yet" << endl;
        return;
    }

    ifstream file(logPath);
    string line;
    while (getline(file, line))
    {
        cout << line << endl;
    }
}

// Restores the working directory to the state of a specific commit
void cmdCheckout(const string &commitSha)
{
    CommitInfo info = parseCommit(commitSha);
    if (info.treeSha.empty())
    {
        cerr << "Error: Invalid commit " << commitSha << endl;
        return;
    }

    // REVISED CLEANUP STEP: Implements file deletion (Case 3) to achieve "exact snapshot" restoration
    // This loops through the working directory and deletes all files/folders not explicitly excluded.
    for (const auto &entry : fs::directory_iterator("."))
    {
        string name = entry.path().filename().string();

        // CRITICAL EXCLUSION: Skip internal repo, executable, source, and makefile to prevent self-deletion and stability issues.
        if (name == "." || name == ".." || name == REPO_DIR ||
            name == "mygit.exe" || name == "main.cpp" || name == "makefile")
            continue;

        try
        {
            // Recursively delete files and directories
            fs::remove_all(entry.path());
        }
        catch (const fs::filesystem_error &e)
        {
            cerr << "Warning: Could not remove " << name << " during checkout: " << e.what() << endl;
        }
    }

    // RESTORE STEP: Rebuilds the directory structure and files from the target commit's Tree
    restoreTree(info.treeSha);

    // Update HEAD to point to the checked out commit
    updateHEAD(commitSha);

    cout << "Checked out commit " << commitSha << endl;
}

// ============= MAIN =============

int main(int argc, char *argv[])
{
    // ... (argument parsing logic) ...
    if (argc < 2)
    {
        cerr << "Usage: mygit <command> [options]" << endl;
        return 1;
    }

    string command = argv[1];

    if (command == "init")
    {
        cmdInit();
    }
    else if (command == "hash-object")
    {
        bool write = false;
        string filepath;
        for (int i = 2; i < argc; i++)
        {
            string arg = argv[i];
            if (arg == "-w")
                write = true;
            else
                filepath = arg;
        }
        cmdHashObject(filepath, write);
    }
    else if (command == "cat-file")
    {
        if (argc < 4)
        {
            cerr << "Usage: mygit cat-file <-p|-s|-t> <sha>" << endl;
            return 1;
        }
        string flag = argv[2];
        string sha = argv[3];
        cmdCatFile(sha, flag[1]);
    }
    else if (command == "write-tree")
    {
        cmdWriteTree();
    }
    else if (command == "ls-tree")
    {
        bool nameOnly = false;
        string sha;
        for (int i = 2; i < argc; i++)
        {
            string arg = argv[i];
            if (arg == "--name-only")
                nameOnly = true;
            else
                sha = arg;
        }
        cmdLsTree(sha, nameOnly);
    }
    else if (command == "add")
    {
        vector<string> paths;
        for (int i = 2; i < argc; i++)
        {
            paths.push_back(argv[i]);
        }
        cmdAdd(paths);
    }
    else if (command == "commit")
    {
        string message = "Initial commit";
        for (int i = 2; i < argc; i++)
        {
            if (string(argv[i]) == "-m" && i + 1 < argc)
            {
                message = argv[i + 1];
                break;
            }
        }
        cmdCommit(message);
    }
    else if (command == "log")
    {
        cmdLog();
    }
    else if (command == "checkout")
    {
        if (argc < 3)
        {
            cerr << "Usage: mygit checkout <commit-sha>" << endl;
            return 1;
        }
        cmdCheckout(argv[2]);
    }
    else
    {
        cerr << "Unknown command: " << command << endl;
        return 1;
    }

    return 0;
}
