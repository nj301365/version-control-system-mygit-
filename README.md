# version-control-system-mygit-
This project involved designing and implementing a simplified version control system that mimics Git's core object model and workflow. The system was built using C++ and utilized the SHA-1 algorithm for content addressing and data integrity, and a compression algorithm (zlib) for efficient object storage.


# Core Concepts

Content Addressing: All file content is stored as immutable Blob objects, named by their SHA-1 hash.

Snapshots: Directory structures are stored as Tree objects, and historical records are kept as Commit objects.

Staging Area: The index file tracks changes prepared for the next commit (add command).

# 🛠️ Compilation and Execution Instructions

The project uses external libraries for SHA-1 hashing (openssl/sha.h) and compression (zlib.h). A makefile is required to compile the code with the appropriate linker options.

# 1. Prerequisites

You must have the following installed on your system:

A C++ compiler (like g++ or MinGW).

The OpenSSL library and headers.

The zlib library and headers.

# 2. Compilation

Assuming you have a standard makefile that links the libraries (-lssl, -lcrypto, -lz), compile the code:

g++ -std=c++17 main.cpp -o mygit.exe -lws2_32 -lssl -lcrypto -lz -lstdc++fs

3. Execution
   All commands are executed using the compiled binary, e.g., ./mygit.exe <command> [options].

# ⚙️ Implemented Commands (Line-by-Line Guide)

The following sequence demonstrates all implemented commands based on the assignment requirements.

1. Initialization (init)
   Initializes the repository structure (.mygit/).

# Command: Initialize the repository

./mygit.exe init

# Expected Output: Initialized empty repository in .mygit

2. Hash-Object (hash-object)
   Calculates SHA-1 and stores the file as a Blob object (-w flag).

# Create a test file

echo -n "VCS test data" > test.txt

# Command: Calculate SHA-1 and write the object

./mygit.exe hash-object -w test.txt

# Expected Output: SHA-1 hash of the content (e.g., d4e510d9f45d828c2e8c2084c8d5040e6093d5f8)

3. Add Files (add)
   Stages a file by updating the index with its metadata and Blob SHA.

# Command: Stage the file

./mygit.exe add test.txt

# The Index file now tracks test.txt.

4. Commit Changes (C1) (commit)
   Creates a Tree object from the index, creates a Commit object, and updates HEAD.

# Command: Create the first commit (C1)

./mygit.exe commit -m "C1: Initial file added"

# Expected Output: SHA-1 hash of the new commit (e.g., 51f3c3a9f07d2c31e9c9c8e11a141a4a49931343)

# (Note: This SHA is C1_SHA)

5. Cat-File (cat-file)
   Reads and displays content (-p), type (-t), or size (-s) of an object.

./mygit.exe cat-file -p 51f3c3a9f07d2c31e9c9c8e11a141a4a49931343

# Command: Show object type (should be 'tree')

./mygit.exe cat-file -t C1_TREE_SHA

# Expected Output: tree

# Command: Print object content

./mygit.exe cat-file -p C1_TREE_SHA

# Expected Output: content of file

6. List Tree (ls-tree)
   Lists contents of a Tree object.

# Command: List detailed contents of the Tree

./mygit.exe ls-tree C1_TREE_SHA

# Expected Output: mode type sha name (e.g., 100644 blob d4e510d9... test.txt)

# Command: List names only (--name-only flag)

./mygit.exe ls-tree --name-only C1_TREE_SHA

# Expected Output: test.txt

7. Commit Changes (C2) and Log
   Introduces a second commit (C2) to test history and checkout.

# Modify the file (for update test)

echo "new data" >> test.txt

# Create a new file (for delete test)

echo "new file" > new_file.txt

# Stage all changes

./mygit.exe add test.txt new_file.txt

# Command: Create the second commit (C2)

./mygit.exe commit -m "C2: Update test.txt and add new_file.txt"

# Expected Output: SHA-1 hash of the new commit (C2_SHA)

# Command: Display the commit history (log)

./mygit.exe log

# Expected Output: Details of C2 followed by C1 (latest to oldest)

8. Checkout Command (checkout)
   Restores the working directory to the state of the older commit (C1). This tests file Update (reverting test.txt) and file Deletion (removing new_file.txt).

# Command: Check out the older commit (C1_SHA)

./mygit.exe checkout 51f3c3a9f07d2c31e9c9c8e11a141a4a49931343

# Expected Output: Checked out commit 51f3c3a9f07d2c31e9c9c8e11a141a4a49931343

# Verification: new_file.txt should be gone (Deletion) and test.txt reverted (Update)

ls new_file.txt # Should result in "No such file or directory"
cat test.txt # Should output "VCS test data" (original content)
