
/*
    skuDB - skuba db 🐈
    Author : Shikhar Upadhyay
*/

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

using namespace std;

const uint32_t COLUMN_USERNAME_SIZE = 32;
const uint32_t COLUMN_EMAIL_SIZE = 255;

enum class ExecuteResult {
  EXECUTE_TABLE_FULL,
  EXECUTE_SUCCESS,
  EXECUTE_DUPLICATE_KEY,
  EXECUTE_UNSUPPORTED_COMMAND
};

enum class MetaCommandResult {
  META_COMMAND_SUCCESS,
  META_COMMAND_UNRECOGNIZED_COMMAND
};

enum class PrepareResult {
  PREPARE_SUCCESS,
  PREPARE_UNRECOGNIZED_STATEMENT,
  PREPARE_SYNTAX_ERROR,
  PREPARE_STRING_TOO_LONG,
  PREPARE_NEGATIVE_ID
};

enum class StatementType { STATEMENT_INSERT, STATEMENT_SELECT };

// Explicitly 1 byte wide so it maps directly to our node header byte!
enum class NodeType : uint8_t { NODE_INTERNAL, NODE_LEAF };

// ============================================================================
// LAYOUT CONSTANTS
// ============================================================================
const uint32_t ID_SIZE = sizeof(uint32_t);
const uint32_t USERNAME_SIZE = COLUMN_USERNAME_SIZE;
const uint32_t EMAIL_SIZE = COLUMN_EMAIL_SIZE;
const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;

const uint32_t PAGE_SIZE = 4096;
const uint32_t TABLE_MAX_PAGES = 100;

// Common Node Header Layout
const uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);
const uint32_t NODE_TYPE_OFFSET = 0;
const uint32_t IS_ROOT_SIZE = sizeof(uint8_t);
const uint32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE;
const uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t);
const uint32_t PARENT_POINTER_OFFSET = IS_ROOT_OFFSET + IS_ROOT_SIZE;
const uint32_t COMMON_NODE_HEADER_SIZE =
    NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE;

// Leaf Node Header Layout
const uint32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_NEXT_LEAF_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NEXT_LEAF_OFFSET =
    LEAF_NODE_NUM_CELLS_SIZE + LEAF_NODE_NUM_CELLS_OFFSET;
const uint32_t LEAF_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE +
                                       LEAF_NODE_NUM_CELLS_SIZE +
                                       LEAF_NODE_NEXT_LEAF_SIZE;

// Leaf Node Body Layout
const uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_KEY_OFFSET = 0;
const uint32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE;
const uint32_t LEAF_NODE_VALUE_OFFSET =
    LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE;
const uint32_t LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE;
const uint32_t LEAF_NODE_SPACE_FOR_CELLS = PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_MAX_CELLS =
    LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE;
const uint32_t LEAF_NODE_RIGHT_SPLIT_COUNT = (LEAF_NODE_MAX_CELLS + 1) / 2;
const uint32_t LEAF_NODE_LEFT_SPLIT_COUNT =
    (LEAF_NODE_MAX_CELLS + 1) - LEAF_NODE_RIGHT_SPLIT_COUNT;

// Internal Node Header Layout
const uint32_t INTERNAL_NODE_NUM_KEYS_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_NUM_KEYS_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t INTERNAL_NODE_RIGHT_CHILD_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_RIGHT_CHILD_OFFSET =
    INTERNAL_NODE_NUM_KEYS_OFFSET + INTERNAL_NODE_NUM_KEYS_SIZE;
const uint32_t INTERNAL_NODE_HEADER_SIZE = COMMON_NODE_HEADER_SIZE +
                                           INTERNAL_NODE_NUM_KEYS_SIZE +
                                           INTERNAL_NODE_RIGHT_CHILD_SIZE;

// Internal Node Body Layout
const uint32_t INTERNAL_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_CHILD_SIZE = sizeof(uint32_t);
const uint32_t INTERNAL_NODE_CELL_SIZE =
    INTERNAL_NODE_KEY_SIZE + INTERNAL_NODE_CHILD_SIZE;
const uint32_t INTERNAL_NODE_MAX_KEYS = 3;

// ============================================================================
// 1. ROW: Encapsulates row data and its binary serialization
// ============================================================================
struct Row {
  uint32_t id = 0;
  char username[COLUMN_USERNAME_SIZE]{};
  char email[COLUMN_EMAIL_SIZE]{};

  void serialize(uint8_t *dest) const {
    memcpy(dest + ID_OFFSET, &id, ID_SIZE);
    memcpy(dest + USERNAME_OFFSET, username, USERNAME_SIZE);
    memcpy(dest + EMAIL_OFFSET, email, EMAIL_SIZE);
  }

  void deserialize(const uint8_t *src) {
    memcpy(&id, src + ID_OFFSET, ID_SIZE);
    memcpy(username, src + USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(email, src + EMAIL_OFFSET, EMAIL_SIZE);
  }

  void print() const {
    cout << "(" << id << ", " << username << ", " << email << ")" << endl;
  }
};

struct Statement {
  StatementType type;
  Row row_to_insert;
};

// ============================================================================
// 2. PAGER: Owns the disk file and 4KB memory pages (RAII)
// ============================================================================
class Pager {
private:
  FILE *fileDescriptor = nullptr;
  long fileLength = 0;
  uint32_t numPages = 0;
  uint8_t *pages[TABLE_MAX_PAGES]{}; // Brace init sets all pointers to nullptr

public:
  explicit Pager(const char *filename) {
    fileDescriptor = fopen(filename, "r+b");
    if (fileDescriptor == nullptr) {
      fileDescriptor = fopen(filename, "w+b");
    }
    if (fileDescriptor == nullptr) {
      cerr << "Unable to open file\n";
      exit(EXIT_FAILURE);
    }

    fseek(fileDescriptor, 0, SEEK_END);
    fileLength = ftell(fileDescriptor);
    fseek(fileDescriptor, 0, SEEK_SET);

    if (fileLength % PAGE_SIZE != 0) {
      cout << "DB file is not a whole number of pages. Corrupt file." << endl;
      exit(EXIT_FAILURE);
    }

    numPages = fileLength / PAGE_SIZE;
  }

  ~Pager() {
    for (uint32_t i = 0; i < numPages; i++) {
      if (pages[i] != nullptr) {
        flush(i);
        delete[] pages[i];
        pages[i] = nullptr;
      }
    }

    if (fclose(fileDescriptor) == -1) {
      cout << "Error closing db file\n";
      exit(EXIT_FAILURE);
    }

    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
      if (pages[i] != nullptr) {
        delete[] pages[i];
        pages[i] = nullptr;
      }
    }
  }

  uint8_t *getPage(uint32_t pageNum) {
    if (pageNum >= TABLE_MAX_PAGES) {
      cout << "Tried to fetch page number out of bounds. " << pageNum
           << " >= " << TABLE_MAX_PAGES << endl;
      exit(EXIT_FAILURE);
    }

    if (pages[pageNum] == nullptr) {
      uint8_t *page = new uint8_t[PAGE_SIZE]();
      uint32_t savedPages = fileLength / PAGE_SIZE;

      if (pageNum < savedPages) {
        fseek(fileDescriptor, pageNum * PAGE_SIZE, SEEK_SET);
        size_t bytesRead = fread(page, 1, PAGE_SIZE, fileDescriptor);
        (void)bytesRead;
      }

      pages[pageNum] = page;

      if (pageNum >= numPages) {
        numPages = pageNum + 1;
      }
    }

    return pages[pageNum];
  }

  void flush(uint32_t pageNum) {
    if (pages[pageNum] == nullptr) {
      cout << "Tried to flush null\n";
      exit(EXIT_FAILURE);
    }
    fseek(fileDescriptor, pageNum * PAGE_SIZE, SEEK_SET);
    fwrite(pages[pageNum], 1, PAGE_SIZE, fileDescriptor);
  }

  uint32_t getNumPages() const { return numPages; }
  uint32_t getUnusedPageNum() const { return numPages; }
};

// ============================================================================
// 3. B-TREE NODE VIEW CLASSES: Encapsulate all byte-offset pointer arithmetic
// ============================================================================

// Base class representing the 6-byte Common Node Header shared by all nodes
class Node {
protected:
  uint8_t *data; // Non-owning pointer to a 4KB page inside Pager

public:
  explicit Node(uint8_t *pageData) : data(pageData) {}

  uint8_t *raw() const { return data; }

  NodeType getType() const {
    return static_cast<NodeType>(data[NODE_TYPE_OFFSET]);
  }

  void setType(NodeType type) {
    data[NODE_TYPE_OFFSET] = static_cast<uint8_t>(type);
  }

  bool isRoot() const { return static_cast<bool>(data[IS_ROOT_OFFSET]); }

  void setRoot(bool root) { data[IS_ROOT_OFFSET] = static_cast<uint8_t>(root); }

  // Useful for upcoming B+ Tree chapters when you need parent pointers!
  uint32_t &parent() {
    return *reinterpret_cast<uint32_t *>(data + PARENT_POINTER_OFFSET);
  }
};

// Derived view for Leaf Nodes
class LeafNode : public Node {
public:
  explicit LeafNode(uint8_t *pageData) : Node(pageData) {}

  void initialize() {
    setType(NodeType::NODE_LEAF);
    setRoot(false);
    numCells() = 0;
    nextLeaf() = 0;
  }

  uint32_t &numCells() {
    return *reinterpret_cast<uint32_t *>(data + LEAF_NODE_NUM_CELLS_OFFSET);
  }

  uint8_t *cell(uint32_t cellNum) {
    return data + LEAF_NODE_HEADER_SIZE + (cellNum * LEAF_NODE_CELL_SIZE);
  }

  uint32_t &key(uint32_t cellNum) {
    return *reinterpret_cast<uint32_t *>(cell(cellNum));
  }

  uint8_t *value(uint32_t cellNum) {
    return cell(cellNum) + LEAF_NODE_KEY_SIZE;
  }

  uint32_t getMaxKey() { return key(numCells() - 1); }

  uint32_t &nextLeaf() {
    return *reinterpret_cast<uint32_t *>(data + LEAF_NODE_NEXT_LEAF_OFFSET);
  }
};

// Derived view for Internal Nodes
class InternalNode : public Node {
public:
  explicit InternalNode(uint8_t *pageData) : Node(pageData) {}

  void initialize() {
    setType(NodeType::NODE_INTERNAL);
    setRoot(false);
    numKeys() = 0;
  }

  uint32_t &numKeys() {
    return *reinterpret_cast<uint32_t *>(data + INTERNAL_NODE_NUM_KEYS_OFFSET);
  }

  uint32_t &rightChild() {
    return *reinterpret_cast<uint32_t *>(data +
                                         INTERNAL_NODE_RIGHT_CHILD_OFFSET);
  }

  uint8_t *cell(uint32_t cellNum) {
    return data + INTERNAL_NODE_HEADER_SIZE +
           (cellNum * INTERNAL_NODE_CELL_SIZE);
  }

  uint32_t &child(uint32_t childNum) {
    uint32_t keys = numKeys();
    if (childNum > keys) {
      cout << "Tried to access childNum " << childNum << " > numKeys " << keys
           << endl;
      exit(EXIT_FAILURE);
    } else if (childNum == keys) {
      return rightChild();
    } else {
      return *reinterpret_cast<uint32_t *>(cell(childNum));
    }
  }

  // Return the index of  child which should contain this key
  uint32_t findChild(uint32_t keyNum) {
    uint32_t keys = numKeys();

    uint32_t minIndex = 0;
    uint32_t maxIndex = keys;

    while(minIndex != maxIndex) {
      uint32_t index = (minIndex + maxIndex) / 2;
      uint32_t keyToRight = key(index);
      if(keyToRight >= keyNum) {
        maxIndex = index;
      } else {
        minIndex = index + 1;
      }
    }

    return minIndex;
  }

  // Fixed pointer math: cell(keyNum) is uint8_t*, so + INTERNAL_NODE_CHILD_SIZE
  // moves forward by exactly 4 bytes!
  uint32_t &key(uint32_t keyNum) {
    return *reinterpret_cast<uint32_t *>(cell(keyNum) +
                                         INTERNAL_NODE_CHILD_SIZE);
  }

  uint32_t getMaxKey() { return key(numKeys() - 1); }
};

// Helper to get the max key of any page regardless of whether it's Leaf or
// Internal
uint32_t getNodeMaxKey(uint8_t *pageData) {
  Node node(pageData);
  if (node.getType() == NodeType::NODE_INTERNAL) {
    return InternalNode(pageData).getMaxKey();
  } else {
    return LeafNode(pageData).getMaxKey();
  }
}

// ============================================================================
// 4. CURSOR & TABLE CLASSES: Manage B+ Tree traversal, splitting, and roots
// ============================================================================

class Table; // Forward declaration so Cursor knows Table exists

class Cursor {
public:
  Table *table;
  uint32_t pageNum;
  uint32_t cellNum;
  bool endOfTable;

  Cursor(Table *tbl, uint32_t pg, uint32_t cell, bool end = false)
      : table(tbl), pageNum(pg), cellNum(cell), endOfTable(end) {}

  uint8_t *value();
  void advance();
  void insert(uint32_t key, const Row &row);

private:
  void splitAndInsert(uint32_t key, const Row &row);
};

class Table {
public:
  Pager *pager;
  uint32_t rootPageNum;

  // Constructor replaces openDB()
  explicit Table(const char *filename) {
    pager = new Pager(filename);
    rootPageNum = 0;

    if (pager->getNumPages() == 0) {
      // Database file is brand new. Initialize page 0 as a root leaf node.
      LeafNode rootNode(pager->getPage(0));
      rootNode.initialize();
      rootNode.setRoot(true);
    }
  }

  // Destructor replaces dbClose()! Deleting pager triggers ~Pager()
  // automatically.
  ~Table() { delete pager; }

  // Replaces tableStart() - returns Cursor by value (no 'new' or memory leaks!)
  Cursor start() {
    Cursor cursor = find(0);
    LeafNode leafNode(pager->getPage(cursor.pageNum));
    uint32_t numCells = leafNode.numCells();
    cursor.endOfTable = (numCells == 0);
    return cursor;
  }

  // Replaces tableFind()
  Cursor find(uint32_t key) {
    uint8_t *rootPage = pager->getPage(rootPageNum);
    Node rootNode(rootPage);

    if (rootNode.getType() == NodeType::NODE_LEAF) {
      return findInLeaf(rootPageNum, key);
    } else {
      return findInInternal(rootPageNum, key);
    }
  }

  // Replaces createNewRoot()
  void createNewRoot(uint32_t rightChildPageNum) {
    uint8_t *rootPage = pager->getPage(rootPageNum);
    uint32_t leftChildPageNum = pager->getUnusedPageNum();
    uint8_t *leftChildPage = pager->getPage(leftChildPageNum);
    uint8_t *rightChildPage = pager->getPage(rightChildPageNum);

    // Copy old root data into leftChildPage
    memcpy(leftChildPage, rootPage, PAGE_SIZE);
    Node leftChild(leftChildPage);
    leftChild.setRoot(false);

    // get rightChild
    Node rightChild(rightChildPage);

    // Re-initialize root page as an Internal Node with 1 key and 2 children
    InternalNode rootNode(rootPage);
    rootNode.initialize();
    rootNode.setRoot(true);
    rootNode.numKeys() = 1;
    rootNode.child(0) = leftChildPageNum;
    rootNode.key(0) = getNodeMaxKey(leftChildPage);
    rootNode.rightChild() = rightChildPageNum;
    leftChild.parent() = rootPageNum;
    rightChild.parent() = rootPageNum;
  }

  void printTree(uint32_t pageNum = 0, uint32_t indentationLevel = 0) {
    uint8_t *page = pager->getPage(pageNum);
    Node node(page);

    if (node.getType() == NodeType::NODE_LEAF) {
      LeafNode leaf(page);
      uint32_t numKeys = leaf.numCells();
      indent(indentationLevel);
      cout << "- leaf (size " << numKeys << ")" << endl;
      for (uint32_t i = 0; i < numKeys; i++) {
        indent(indentationLevel + 1);
        cout << "- " << leaf.key(i) << endl;
      }
    } else {
      InternalNode internal(page);
      uint32_t numKeys = internal.numKeys();
      indent(indentationLevel);
      cout << "- internal (size " << numKeys << ")" << endl;
      for (uint32_t i = 0; i < numKeys; i++) {
        printTree(internal.child(i), indentationLevel + 1);
        indent(indentationLevel + 1);
        cout << "- key " << internal.key(i) << endl;
      }
      printTree(internal.rightChild(), indentationLevel + 1);
    }
  }

  // add a new child/key air to parent that corresponds to child
  void insertIntoInternal(uint32_t parentPageNum, uint32_t childPageNum) {
    InternalNode parent(pager->getPage(parentPageNum));
    LeafNode child(pager->getPage(childPageNum));
    uint32_t childMaxKey = child.getMaxKey();
    uint32_t index = parent.findChild(childMaxKey);

    uint32_t originalNumKeys = parent.numKeys();
    parent.numKeys() = originalNumKeys + 1;

    if (originalNumKeys >= INTERNAL_NODE_MAX_KEYS) {
      cout << "Need to implement splitting internal node" << endl;
      exit(EXIT_FAILURE);
    }

    uint32_t rightChildPageNum = parent.rightChild();
    LeafNode rightChild(pager->getPage(rightChildPageNum));
    uint32_t rightChildMaxKey = rightChild.getMaxKey();

    if (childMaxKey > rightChildMaxKey) {
      parent.child(originalNumKeys) = rightChildPageNum;
      parent.key(originalNumKeys) = rightChildMaxKey;
      parent.rightChild() = childPageNum;
    } else {
      for (uint32_t i = originalNumKeys; i > index; i--) {
        uint8_t *destination = parent.cell(i);
        uint8_t *source = parent.cell(i - 1);
        memcpy(destination, source, INTERNAL_NODE_CELL_SIZE);
      }

      parent.child(index) = childPageNum;
      parent.key(index) = childMaxKey;
    }
  }

  void updateInternalNodeKey(InternalNode node, uint32_t oldKey,
                             uint32_t newKey) {
    uint32_t oldChildIndex = node.child(oldKey);
    node.key(oldChildIndex) = newKey;
  }

private:
  static void indent(uint32_t level) {
    for (uint32_t i = 0; i < level; i++) {
      cout << "  ";
    }
  }

  // Replaces leafNodeFind()
  Cursor findInLeaf(uint32_t pageNum, uint32_t key) {
    LeafNode node(pager->getPage(pageNum));
    uint32_t numCells = node.numCells();

    uint32_t minIndex = 0;
    uint32_t onePastMaxIndex = numCells;

    while (onePastMaxIndex != minIndex) {
      uint32_t index = (minIndex + onePastMaxIndex) / 2;
      uint32_t keyAtIndex = node.key(index);
      if (key == keyAtIndex) {
        return Cursor(this, pageNum, index);
      } else if (key < keyAtIndex) {
        onePastMaxIndex = index;
      } else {
        minIndex = index + 1;
      }
    }

    return Cursor(this, pageNum, minIndex);
  }

  Cursor findInInternal(uint32_t pageNum, uint32_t key) {
    InternalNode node(pager->getPage(pageNum));
    uint32_t numKeys = node.numKeys();

    /* Binary search to find index of child to search*/
    uint32_t minIndex = 0;
    uint32_t maxIndex = numKeys;

    while (minIndex != maxIndex) {
      uint32_t midIndex = (minIndex + maxIndex) / 2;
      uint32_t keyToRight = node.key(midIndex);

      if (keyToRight >= key) {
        maxIndex = midIndex;
      } else {
        minIndex = midIndex + 1;
      }
    }

    uint32_t childIndex = node.findChild(key);
    uint32_t childNum = node.child(childIndex);

    Node child(pager->getPage(childNum));

    switch (child.getType()) {
    case NodeType::NODE_LEAF:
      return findInLeaf(childNum, key);
    case NodeType::NODE_INTERNAL:
      return findInInternal(childNum, key);
    }

    return Cursor(this, pageNum, minIndex);
  }

  
};

// ============================================================================
// CURSOR METHOD IMPLEMENTATIONS
// ============================================================================

inline uint8_t *Cursor::value() {
  LeafNode node(table->pager->getPage(pageNum));
  return node.value(cellNum);
}

inline void Cursor::advance() {
  LeafNode node(table->pager->getPage(pageNum));
  cellNum++;
  if (cellNum >= node.numCells()) {
    uint32_t nextPageNum = node.nextLeaf();
    if (nextPageNum == 0) {
      // this was rightmost leaf
      endOfTable = true;
    } else {
      pageNum = nextPageNum;
      cellNum = 0;
    }
  }
}

// Replaces leafNodeInsert()
inline void Cursor::insert(uint32_t key, const Row &row) {
  LeafNode node(table->pager->getPage(pageNum));
  uint32_t numCells = node.numCells();

  if (numCells >= LEAF_NODE_MAX_CELLS) {
    splitAndInsert(key, row);
    return;
  }

  if (cellNum < numCells) {
    for (uint32_t i = numCells; i > cellNum; i--) {
      memcpy(node.cell(i), node.cell(i - 1), LEAF_NODE_CELL_SIZE);
    }
  }

  node.numCells() += 1;
  node.key(cellNum) = key;
  row.serialize(node.value(cellNum));
}

// Replaces leafNodeSplitAndInsert()
inline void Cursor::splitAndInsert(uint32_t key, const Row &row) {
  LeafNode oldNode(table->pager->getPage(pageNum));
  uint32_t oldMaxKey = oldNode.getMaxKey();
  uint32_t newPageNum = table->pager->getUnusedPageNum();
  uint32_t rootPageNum = table->rootPageNum;
  LeafNode newNode(table->pager->getPage(newPageNum));
  newNode.initialize();
  newNode.parent() = oldNode.parent();
  newNode.nextLeaf() = oldNode.nextLeaf();
  oldNode.nextLeaf() = newPageNum;

  for (int32_t i = LEAF_NODE_MAX_CELLS; i >= 0; i--) {
    LeafNode &destNode = (i >= static_cast<int32_t>(LEAF_NODE_LEFT_SPLIT_COUNT))
                             ? newNode
                             : oldNode;
    uint32_t indexWithinNode = i % LEAF_NODE_LEFT_SPLIT_COUNT;

    if (i == static_cast<int32_t>(cellNum)) {
      row.serialize(destNode.value(indexWithinNode));
      destNode.key(indexWithinNode) = key;
    } else if (i > static_cast<int32_t>(cellNum)) {
      memcpy(destNode.cell(indexWithinNode), oldNode.cell(i - 1),
             LEAF_NODE_CELL_SIZE);
    } else {
      memcpy(destNode.cell(indexWithinNode), oldNode.cell(i),
             LEAF_NODE_CELL_SIZE);
    }
  }

  oldNode.numCells() = LEAF_NODE_LEFT_SPLIT_COUNT;
  newNode.numCells() = LEAF_NODE_RIGHT_SPLIT_COUNT;

  if (oldNode.isRoot()) {
    table->createNewRoot(newPageNum);
  } else {
    uint32_t parentPageNum = oldNode.parent();
    uint32_t newMaxKey = oldNode.getMaxKey();
    InternalNode parent(table->pager->getPage(parentPageNum));

    table->updateInternalNodeKey(parent, oldMaxKey, newMaxKey);
    table->insertIntoInternal(parentPageNum, newPageNum);
    return;
  }
}

// ============================================================================
// REPL & EXECUTION FUNCTIONS
// ============================================================================
void printPrompt() { cout << "^_^ : "; }

void readInputBuffer(string &inputBuffer) {
  getline(cin, inputBuffer);
  if (inputBuffer.length() <= 0) {
    cout << "\nError reading input\n";
    exit(EXIT_FAILURE);
  }
}

MetaCommandResult doMetaCommand(string &inputBuffer, Table *table) {
  if (inputBuffer == ".exit") {
    delete table; // Triggers Pager destructor, flushes pages, and closes file
                  // cleanly
    cout << "Exiting...\nMeowwww angyyyyy!\n😾😾😾😾😾\n";
    exit(EXIT_SUCCESS);
  } else if (inputBuffer == ".btree") {
    cout << "Tree: \n";
    table->printTree(table->rootPageNum, 0);
    return MetaCommandResult::META_COMMAND_SUCCESS;
  } else if (inputBuffer == ".constants") {
    cout << "Constants: \n";
    cout << "ROW_SIZE: " << ROW_SIZE << endl;
    cout << "COMMON_NODE_HEADER_SIZE: " << COMMON_NODE_HEADER_SIZE << endl;
    cout << "LEAF_NODE_HEADER_SIZE: " << LEAF_NODE_HEADER_SIZE << endl;
    cout << "LEAF_NODE_CELL_SIZE: " << LEAF_NODE_CELL_SIZE << endl;
    cout << "LEAF_NODE_SPACE_FOR_CELLS: " << LEAF_NODE_SPACE_FOR_CELLS << endl;
    cout << "LEAF_NODE_MAX_CELLS: " << LEAF_NODE_MAX_CELLS << endl;
    return MetaCommandResult::META_COMMAND_SUCCESS;
  } else {
    return MetaCommandResult::META_COMMAND_UNRECOGNIZED_COMMAND;
  }
}

PrepareResult prepareStatement(string inputBuffer, Statement *statement) {
  string command;
  stringstream ss(inputBuffer);
  ss >> command;

  if (command == "insert") {
    statement->type = StatementType::STATEMENT_INSERT;
    int id;
    string usernameStr, emailStr;
    if (ss >> id >> usernameStr >> emailStr) {
      if (usernameStr.length() >= COLUMN_USERNAME_SIZE) {
        return PrepareResult::PREPARE_STRING_TOO_LONG;
      }
      if (emailStr.length() >= COLUMN_EMAIL_SIZE) {
        return PrepareResult::PREPARE_STRING_TOO_LONG;
      }
      if (id < 0) {
        return PrepareResult::PREPARE_NEGATIVE_ID;
      }

      statement->row_to_insert.id = static_cast<uint32_t>(id);
      strcpy(statement->row_to_insert.username, usernameStr.c_str());
      strcpy(statement->row_to_insert.email, emailStr.c_str());
      return PrepareResult::PREPARE_SUCCESS;
    } else {
      return PrepareResult::PREPARE_SYNTAX_ERROR;
    }
  } else if (command == "select") {
    statement->type = StatementType::STATEMENT_SELECT;
    return PrepareResult::PREPARE_SUCCESS;
  }

  return PrepareResult::PREPARE_UNRECOGNIZED_STATEMENT;
}

ExecuteResult executeInsert(Statement *statement, Table *table) {
  Row &rowToInsert = statement->row_to_insert;
  uint32_t keyToInsert = rowToInsert.id;
  Cursor cursor = table->find(keyToInsert);

  LeafNode node(table->pager->getPage(cursor.pageNum));
  if (cursor.cellNum < node.numCells()) {
    if (node.key(cursor.cellNum) == keyToInsert) {
      return ExecuteResult::EXECUTE_DUPLICATE_KEY;
    }
  }

  cursor.insert(keyToInsert, rowToInsert);
  cout << "\nInserted 1 row succesfully!" << endl;
  return ExecuteResult::EXECUTE_SUCCESS;
}

ExecuteResult executeSelect(Statement *statement, Table *table) {
  Cursor cursor = table->start();
  Row row;
  uint32_t count = 0;

  cout << endl;
  while (!cursor.endOfTable) {
    row.deserialize(cursor.value());
    row.print();
    cursor.advance();
    count++;
  }
  cout << "\n" << count << " rows projected!" << endl;
  return ExecuteResult::EXECUTE_SUCCESS;
}

ExecuteResult executeStatement(Statement *statement, Table *table) {
  switch (statement->type) {
  case StatementType::STATEMENT_INSERT:
    return executeInsert(statement, table);
  case StatementType::STATEMENT_SELECT:
    return executeSelect(statement, table);
  default:
    return ExecuteResult::EXECUTE_UNSUPPORTED_COMMAND;
  }
}

// ============================================================================
// MAIN REPL LOOP
// ============================================================================
int main(int argc, char *argv[]) {
  if (argc < 2) {
    cerr << "Must supply a database filename\n";
    exit(EXIT_FAILURE);
  }

  const char *filename = argv[1];
  Table *table = new Table(filename);
  string inputBuffer = "";

  while (true) {
    cout << endl;
    printPrompt();
    readInputBuffer(inputBuffer);

    if (inputBuffer.empty())
      continue;

    if (inputBuffer[0] == '.') {
      switch (doMetaCommand(inputBuffer, table)) {
      case (MetaCommandResult::META_COMMAND_SUCCESS):
        continue;
      case (MetaCommandResult::META_COMMAND_UNRECOGNIZED_COMMAND):
        cout << "\nUnrecognized meta command '" << inputBuffer << "'" << endl;
        continue;
      }
    }

    Statement statement;
    switch (prepareStatement(inputBuffer, &statement)) {
    case (PrepareResult::PREPARE_SUCCESS):
      break;
    case (PrepareResult::PREPARE_STRING_TOO_LONG):
      cout << "\nString is too long." << endl;
      continue;
    case (PrepareResult::PREPARE_NEGATIVE_ID):
      cout << "\nID must be positive." << endl;
      continue;
    case (PrepareResult::PREPARE_SYNTAX_ERROR):
      cout << "\nSyntax error. Could not parse statement." << endl;
      continue;
    case (PrepareResult::PREPARE_UNRECOGNIZED_STATEMENT):
      cout << "\nUnrecognized keyword at the start of '" << inputBuffer << "'"
           << endl;
      continue;
    }

    switch (executeStatement(&statement, table)) {
    case ExecuteResult::EXECUTE_SUCCESS:
      break;
    case ExecuteResult::EXECUTE_DUPLICATE_KEY:
      cout << "\nError: Duplicate key.\n";
      break;
    case ExecuteResult::EXECUTE_TABLE_FULL:
      cout << "\nError: Table full! Meow" << endl;
      break;
    case ExecuteResult::EXECUTE_UNSUPPORTED_COMMAND:
      cout << "\nError: Unsupported command.\n";
      break;
    }
  }

  delete table;
  return 0;
}
