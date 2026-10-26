/*
    skuDB - skuba db 🐈
    Author : Shikhar Upadhyay
*/

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <strings.h>
#include <sys/types.h>

using namespace std;

const int COLUMN_USERNAME_SIZE = 32;
const int COLUMN_EMAIL_SIZE = 255;

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

enum class NodeType { NODE_INTERNAL, NODE_LEAF };

struct Row {
  u_int32_t id;
  char username[COLUMN_USERNAME_SIZE];
  char email[COLUMN_EMAIL_SIZE];
};

struct Statement {
  StatementType type;
  Row row_to_insert;
};

// compact representation of a row
const u_int32_t ID_SIZE = sizeof(Row::id);
const u_int32_t USERNAME_SIZE = sizeof(Row::username);
const u_int32_t EMAIL_SIZE = sizeof(Row::email);
const u_int32_t ID_OFFSET = 0;
const u_int32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const u_int32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const u_int32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;

const u_int32_t PAGE_SIZE = 4096;
const int TABLE_MAX_PAGES = 100;

// common node header layout
const u_int32_t NODE_TYPE_SIZE = sizeof(uint8_t);
const u_int32_t NODE_TYPE_OFFSET = 0;
const u_int32_t IS_ROOT_SIZE = sizeof(uint8_t);
const u_int32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE;
const u_int32_t PARENT_POINTER_SIZE = sizeof(uint32_t);
const u_int32_t PARENT_POINTER_OFFSET = IS_ROOT_OFFSET + IS_ROOT_SIZE;
const u_int32_t COMMON_NODE_HEADER_SIZE =
    NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE;

// leaf node header layout
const u_int32_t LEAF_NODE_NUM_CELLS_SIZE = sizeof(uint32_t);
const u_int32_t LEAF_NODE_NUM_CELLS_OFFSET = COMMON_NODE_HEADER_SIZE;
const u_int32_t LEAF_NODE_HEADER_SIZE =
    COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE;

// leaf node body layout
const u_int32_t LEAF_NODE_KEY_SIZE = sizeof(u_int32_t);
const u_int32_t LEAF_NODE_KEY_OFFSET = 0;
const u_int32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE;
const u_int32_t LEAF_NODE_VALUE_OFFSET =
    LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE;
const u_int32_t LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE;
const u_int32_t LEAF_NODE_SPACE_FOR_CELLS = PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
const u_int32_t LEAF_NODE_MAX_CELLS =
    LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE;
const u_int32_t LEAF_NODE_RIGHT_SPLIT_COUNT = (LEAF_NODE_MAX_CELLS + 1) / 2;
const u_int32_t LEAF_NODE_LEFT_SPLIT_COUNT =
    (LEAF_NODE_MAX_CELLS + 1) - LEAF_NODE_RIGHT_SPLIT_COUNT;

struct Pager {
  FILE *fileDescriptor;
  long fileLength;
  u_int32_t numPages;
  void *pages[TABLE_MAX_PAGES];
};

struct Table {
  Pager *pager;
  u_int32_t rootPageNum;
};

struct Cursor {
  Table *table;
  u_int32_t pageNum;
  u_int32_t cellNum;
  bool endOfTable;
};

// function declarations

void printPrompt();
void readInputBuffer(string &inputBuffer);
MetaCommandResult doMetaCommand(string &inputBuffer, Table *table);
PrepareResult prepareStatement(string inputBuffer, Statement *statement);
ExecuteResult executeStatement(Statement *statement, Table *table);
ExecuteResult executeInsert(Statement *statement, Table *table);
ExecuteResult executeSelect(Statement *statement, Table *table);
void printRow(Row *row);
void serializeRow(Row *src, void *dest);
void deserializeRow(void *src, Row *dest);
void *cursorValue(Cursor *cursor);
Table *openDB(const char *filename);
void pagerFlush(Pager *pager, int pageNum);
long getFileSize(const char *filename);
Pager *pagerOpen(const char *filename);
void *getPage(Pager *pager, int pageNum);
void dbClose(Table *table);
Cursor *tableStart(Table *table);
void cursorAdvance(Cursor *cursor);
u_int32_t *leafNodeNumCells(void *node);
void *leafNodeCell(void *node, u_int32_t cellNum);
u_int32_t *leafNodeKey(void *node, u_int32_t cellNum);
void *leafNodeValue(void *node, u_int32_t cellNum);
void initializeLeafNode(void *node);
void leafNodeInsert(Cursor *cursor, u_int32_t key, Row *value);
void printConstants();
void printLeafNode(void *node);
Cursor *tableFind(Table *table, u_int32_t key);
Cursor *leafNodeFind(Table *table, u_int32_t pageNum, u_int32_t key);
NodeType getNodeType(void *node);
void setNodeType(void *node, NodeType type);
void leafNodeSplitAndInsert(Cursor *cursor, u_int32_t key, Row *value);
u_int32_t getUnusedPageNum(Pager *pager);

int main(int argc, char *argv[]) {
  if (argc < 2) {
    cerr << "Must supply a database filename\n";
    exit(EXIT_FAILURE);
  }
  const char *filename = argv[1];
  Table *table = openDB(filename);
  string inputBuffer = "";
  while (true) {
    cout << endl;

    printPrompt();
    readInputBuffer(inputBuffer);

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
    }
  }
}

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
    dbClose(table);
    cout << "Exiting..." << endl;
    cout << "Meowwww angyyyyy!" << endl;
    cout << "😾😾😾😾😾\n";
    exit(EXIT_SUCCESS);
  } else if (inputBuffer == ".btree") {
    cout << "Tree: " << endl;
    printLeafNode(getPage(table->pager, 0));
    return MetaCommandResult::META_COMMAND_SUCCESS;
  } else if (inputBuffer == ".constants") {
    cout << "Constants: " << endl;
    printConstants();
    return MetaCommandResult::META_COMMAND_SUCCESS;
  } else {
    return MetaCommandResult::META_COMMAND_UNRECOGNIZED_COMMAND;
  }
}

PrepareResult prepareStatement(string inputBuffer, Statement *statement) {
  int id;
  char username[COLUMN_USERNAME_SIZE];
  char email[COLUMN_EMAIL_SIZE];
  string command;
  stringstream ss(inputBuffer);
  ss >> command;

  if (command == "insert") {
    statement->type = StatementType::STATEMENT_INSERT;
    if (ss >> id >> username >> email) {
      if ((strlen(username) > COLUMN_USERNAME_SIZE) ||
          (strlen(email) > COLUMN_EMAIL_SIZE)) {
        return PrepareResult::PREPARE_STRING_TOO_LONG;
      }

      if (id < 0) {
        return PrepareResult::PREPARE_NEGATIVE_ID;
      }

      statement->row_to_insert.id = id;
      strcpy(statement->row_to_insert.email, email);
      strcpy(statement->row_to_insert.username, username);
      return PrepareResult::PREPARE_SUCCESS;
    } else {
      return PrepareResult::PREPARE_UNRECOGNIZED_STATEMENT;
    }
  } else if (command == "select") {
    statement->type = StatementType::STATEMENT_SELECT;
    return PrepareResult::PREPARE_SUCCESS;
  }

  return PrepareResult::PREPARE_UNRECOGNIZED_STATEMENT;
}

ExecuteResult executeInsert(Statement *statement, Table *table) {
  void *node = getPage(table->pager, table->rootPageNum);
  u_int32_t numCells = *(leafNodeNumCells(node));

  Row *rowToInsert = &(statement->row_to_insert);
  u_int32_t keyToInsert = rowToInsert->id;
  Cursor *cursor = tableFind(table, keyToInsert);

  if (cursor->cellNum < numCells) {
    u_int32_t keyAtIndex = *leafNodeKey(node, cursor->cellNum);
    if (keyAtIndex == keyToInsert) {
      return ExecuteResult::EXECUTE_DUPLICATE_KEY;
    }
  }

  leafNodeInsert(cursor, rowToInsert->id, rowToInsert);

  cout << endl;
  cout << "Inserted 1 row succesfully!" << endl;

  delete cursor;

  return ExecuteResult::EXECUTE_SUCCESS;
}

ExecuteResult executeSelect(Statement *statement, Table *table) {
  Cursor *cursor = tableStart(table);
  Row row;
  void *node = getPage(table->pager, table->rootPageNum);
  u_int32_t numCells = *(leafNodeNumCells(node));

  cout << endl;

  for (u_int32_t i = 0; i < numCells; i++) {
    deserializeRow(cursorValue(cursor), &row);
    printRow(&row);
    cursorAdvance(cursor);
  }
  cout << endl;
  cout << numCells << " rows projected!" << endl;

  delete cursor;

  return ExecuteResult::EXECUTE_SUCCESS;
}

void printRow(Row *row) {
  cout << "(" << row->id << ", " << row->username << ", " << row->email << ")"
       << endl;
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

void serializeRow(Row *src, void *dest) {
  char *destination = static_cast<char *>(dest);

  memcpy(destination + ID_OFFSET, &(src->id), ID_SIZE);
  memcpy(destination + USERNAME_OFFSET, &(src->username), USERNAME_SIZE);
  memcpy(destination + EMAIL_OFFSET, &(src->email), EMAIL_SIZE);
}

void deserializeRow(void *src, Row *dest) {
  const char *source = static_cast<const char *>(src);

  memcpy(&(dest->id), source + ID_OFFSET, ID_SIZE);
  memcpy(&(dest->username), source + USERNAME_OFFSET, USERNAME_SIZE);
  memcpy(&(dest->email), source + EMAIL_OFFSET, EMAIL_SIZE);
}

void *cursorValue(Cursor *cursor) {
  u_int32_t pageNum = cursor->pageNum;
  void *page = getPage(cursor->table->pager, pageNum);

  return leafNodeValue(page, cursor->cellNum);
}

void *getPage(Pager *pager, int pageNum) {
  if (pageNum > TABLE_MAX_PAGES) {
    cout << "Tried to fetch page number out of bounds. " << pageNum << " > "
         << TABLE_MAX_PAGES << endl;
    exit(EXIT_FAILURE);
  }

  if (pager->pages[pageNum] == nullptr) {
    // cache miss. Allocate memory and load from file
    void *page = new char[PAGE_SIZE]();
    int numPages = pager->fileLength / PAGE_SIZE;

    // We might save a partial page at the end of the file
    if (pager->fileLength % PAGE_SIZE) {
      numPages++;
    }

    if (pageNum <= numPages) {
      fseek(pager->fileDescriptor, pageNum * PAGE_SIZE, SEEK_SET);
      int bytesRead = fread(page, 1, PAGE_SIZE, pager->fileDescriptor);
    }
    pager->pages[pageNum] = page;

    if (pageNum >= pager->numPages) {
      pager->numPages = pageNum + 1;
    }
  }

  return pager->pages[pageNum];
}

Table *openDB(const char *filename) {
  Pager *pager = pagerOpen(filename);

  Table *table = new Table();
  table->pager = pager;
  table->rootPageNum = 0;

  if (pager->numPages == 0) {
    void *rootNode = getPage(pager, 0);
    initializeLeafNode(rootNode);
  }

  return table;
}

void dbClose(Table *table) {
  Pager *pager = table->pager;

  for (int i = 0; i < pager->numPages; i++) {
    if (pager->pages[i] == nullptr) {
      continue;
    }
    pagerFlush(pager, i);
    delete[] static_cast<char *>(pager->pages[i]);
    pager->pages[i] = nullptr;
  }

  int result = fclose(pager->fileDescriptor);
  if (result == -1) {
    cout << "Error closing db file\n";
    exit(EXIT_FAILURE);
  }

  for (int i = 0; i < TABLE_MAX_PAGES; i++) {
    void *page = pager->pages[i];
    if (page) {
      delete[] static_cast<char *>(page);
      pager->pages[i] = nullptr;
    }
  }

  delete pager;
  delete table;
}

Pager *pagerOpen(const char *filename) {
  FILE *file = fopen(filename, "r+b");

  if (file == nullptr) {
    file = fopen(filename, "w+b");
  }

  Pager *pager = new Pager();
  pager->fileDescriptor = file;
  pager->fileLength = getFileSize(filename);
  pager->numPages = (pager->fileLength / PAGE_SIZE);

  if (pager->fileLength % PAGE_SIZE != 0) {
    cout << "DB file is not a whole number of pages. Corrupt file." << endl;
    exit(EXIT_FAILURE);
  }

  for (int i = 0; i < TABLE_MAX_PAGES; i++) {
    pager->pages[i] = nullptr;
  }

  return pager;
}

long getFileSize(const char *filename) {
  FILE *fp = fopen(filename, "rb");
  if (fp == nullptr) {
    cout << "Unable to open file\n";
    exit(EXIT_FAILURE);
  }

  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return -1;
  }

  long size = ftell(fp);

  fclose(fp);

  return size;
}

void pagerFlush(Pager *pager, int pageNum) {
  if (pager->pages[pageNum] == nullptr) {
    cout << "Tried to flush null\n";
    exit(EXIT_FAILURE);
  }

  fseek(pager->fileDescriptor, pageNum * PAGE_SIZE, SEEK_SET);

  int bytesWritten =
      fwrite(pager->pages[pageNum], 1, PAGE_SIZE, pager->fileDescriptor);
}

Cursor *tableStart(Table *table) {
  Cursor *cursor = new Cursor();
  cursor->table = table;
  cursor->pageNum = table->rootPageNum;
  cursor->cellNum = 0;

  void *rootNode = getPage(table->pager, table->rootPageNum);
  u_int32_t numCells = *leafNodeNumCells(rootNode);
  cursor->endOfTable = (numCells == 0);

  return cursor;
}

/* Return the position of the givan key. If key not present
 *   then it returns the position it should be inserted at.
 */
Cursor *tableFind(Table *table, u_int32_t key) {
  u_int32_t rootPageNum = table->rootPageNum;
  void *rootNode = getPage(table->pager, rootPageNum);

  if (getNodeType(rootNode) == NodeType::NODE_LEAF) {
    return leafNodeFind(table, rootPageNum, key);
  } else {
    cout << "Need to implement searching internal nodes" << endl;
    exit(EXIT_FAILURE);
  }
}

Cursor *leafNodeFind(Table *table, u_int32_t pageNum, u_int32_t key) {
  void *node = getPage(table->pager, pageNum);
  u_int32_t numCells = *leafNodeNumCells(node);

  Cursor *cursor = new Cursor();
  cursor->table = table;
  cursor->pageNum = pageNum;

  // Binary search
  u_int32_t minIndex = 0;
  u_int32_t onePastMaxIndex = numCells;

  while (onePastMaxIndex != minIndex) {
    u_int32_t index = (minIndex + onePastMaxIndex) / 2;
    u_int32_t keyAtIndex = *leafNodeKey(node, index);
    if (key == keyAtIndex) {
      cursor->cellNum = index;
      return cursor;
    } else if (key < keyAtIndex) {
      onePastMaxIndex = index;
    } else {
      minIndex = index + 1;
    }
  }

  cursor->cellNum = minIndex;
  return cursor;
}

void cursorAdvance(Cursor *cursor) {
  u_int32_t pageNum = cursor->pageNum;
  void *node = getPage(cursor->table->pager, pageNum);
  cursor->cellNum++;

  if (cursor->cellNum >= *leafNodeNumCells(node)) {
    cursor->endOfTable = true;
  }
}

u_int32_t *leafNodeNumCells(void *node) {
  char *bytePtr = static_cast<char *>(node) + LEAF_NODE_NUM_CELLS_OFFSET;

  return reinterpret_cast<u_int32_t *>(bytePtr);
}

void *leafNodeCell(void *node, u_int32_t cellNum) {
  return static_cast<char *>(node) + LEAF_NODE_HEADER_SIZE +
         cellNum * LEAF_NODE_CELL_SIZE;
}

u_int32_t *leafNodeKey(void *node, u_int32_t cellNum) {
  return static_cast<u_int32_t *>(leafNodeCell(node, cellNum));
}

void *leafNodeValue(void *node, u_int32_t cellNum) {
  return static_cast<char *>(leafNodeCell(node, cellNum)) + LEAF_NODE_KEY_SIZE;
}

void initializeLeafNode(void *node) {
  setNodeType(node, NodeType::NODE_LEAF);
  *leafNodeNumCells(node) = 0;
}

void leafNodeInsert(Cursor *cursor, u_int32_t key, Row *value) {
  void *node = getPage(cursor->table->pager, cursor->pageNum);

  u_int32_t numCells = *leafNodeNumCells(node);
  if (numCells >= LEAF_NODE_MAX_CELLS) {
    // node full
    leafNodeSplitAndInsert(cursor, key, value);
    return;
  }

  if (cursor->cellNum < numCells) {
    // make room for new cell
    for (u_int32_t i = numCells; i > cursor->cellNum; i--) {
      memcpy(leafNodeCell(node, i), leafNodeCell(node, i - 1),
             LEAF_NODE_CELL_SIZE);
    }
  }
  *(leafNodeNumCells(node)) += 1;
  *(leafNodeKey(node, cursor->cellNum)) = key;
  serializeRow(value, leafNodeValue(node, cursor->cellNum));
}

void printLeafNode(void *node) {
  u_int32_t numCells = *leafNodeNumCells(node);
  cout << "leaf (size " << numCells << ")" << endl;
  for (u_int32_t i = 0; i < numCells; i++) {
    u_int32_t key = *leafNodeKey(node, i);
    cout << "  - " << i << " : " << key << endl;
  }
}

void printConstants() {
  cout << "ROW_SIZE: " << ROW_SIZE << endl;
  cout << "COMMON_NODE_HEADER_SIZE: " << COMMON_NODE_HEADER_SIZE << endl;
  cout << "LEAF_NODE_HEADER_SIZE: " << LEAF_NODE_HEADER_SIZE << endl;
  cout << "LEAF_NODE_CELL_SIZE: " << LEAF_NODE_CELL_SIZE << endl;
  cout << "LEAF_NODE_SPACE_FOR_CELLS: " << LEAF_NODE_SPACE_FOR_CELLS << endl;
  cout << "LEAF_NODE_MAX_CELLS: " << LEAF_NODE_MAX_CELLS << endl;
}

NodeType getNodeType(void *node) {
  uint8_t value = *(static_cast<uint8_t *>(node) + NODE_TYPE_OFFSET);
  return static_cast<NodeType>(value);
}

void setNodeType(void *node, NodeType type) {
  *(static_cast<uint8_t *>(node) + NODE_TYPE_OFFSET) =
      static_cast<uint8_t>(type);
}

void leafNodeSplitAndInsert(Cursor *cursor, u_int32_t key, Row *value) {
  /*
   * Create a new node and move half the cells over.
   * Insert the new value in one the two nodes.
   * Update parent or create a new parent.
   */
  void *oldNode = getPage(cursor->table->pager, cursor->pageNum);
  u_int32_t newPageNum = getUnusedPageNum(cursor->table->pager);
  void *newNode = getPage(cursor->table->pager, newPageNum);
  initializeLeafNode(newNode);

  /*
   * All existing keys plus new key should be divided
   * evenly between old (left) and new (right) nodes.
   * Startin from the right, move each key to correct position.
   */

  for (int32_t i = LEAF_NODE_MAX_CELLS; i >= 0; i--) {
    void *destinationNode;
    if (i >= LEAF_NODE_LEFT_SPLIT_COUNT) {
      destinationNode = newNode;
    } else {
      destinationNode = oldNode;
    }
    u_int32_t indexWithinNode = i % LEAF_NODE_LEFT_SPLIT_COUNT;
    void *destination = leafNodeCell(destinationNode, indexWithinNode);
    if (i == cursor->cellNum) {
      serializeRow(value, destination);
    } else if (i > cursor->cellNum) {
      memcpy(destination, leafNodeCell(oldNode, i - 1), LEAF_NODE_CELL_SIZE);
    } else {
      memcpy(destination, leafNodeCell(oldNode, i), LEAF_NODE_CELL_SIZE);
    }
  }

  // Update cell count on both leaf nodes
  *(leafNodeNumCells(oldNode)) = LEAF_NODE_LEFT_SPLIT_COUNT;
  *(leafNodeNumCells(newNode)) = LEAF_NODE_RIGHT_SPLIT_COUNT;

  if (isNodeRoot(oldNode)) {
    return createNewRoot(cursor->table, newPageNum);
  } else {
    cout << "Need to implement updating parent after split" << endl;
    exit(EXIT_FAILURE);
  }
}

u_int32_t getUnusedPageNum(Pager *pager) { return pager->numPages; }

void createNewRoot(Table *table, u_int32_t rightChildPageNum) {
  void *root = getPage(table->pager, table->rootPageNum);
  void *rightChild = getPage(table->pager, rightChildPageNum);
  u_int32_t leftChildPageNum = getUnusedPageNum(table->pager);
  void *leftChild = getPage(table->pager, leftChildPageNum);

  memcpy(leftChild, root, PAGE_SIZE);
  setNodeRoot(leftChild, false);

  initializeInternalNode(root);
  setNodeRoot(root, true);
  *internalNodeNumKeys(root) = 1;
  *internalNodeChild(root, 0) = leftChildPageNum;
  u_int32_t leftChildMaxKey = getNodeMaxKey(leftChild);
  *internalNodeKey(root, 0) = leftChildMaxKey;
  *internalNodeChild(root) = rightChildPageNum;
}
