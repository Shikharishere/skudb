/*
    skuDB - skuba db 🐈
    Author : Shikhar Upadhyay
*/ 

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <string>
#include <sstream>
#include <fstream>

using namespace std;

const int COLUMN_USERNAME_SIZE = 32;
const int COLUMN_EMAIL_SIZE = 255;

enum class ExecuteResult {
    EXECUTE_TABLE_FULL,
    EXECUTE_SUCCESS,
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

enum class StatementType {
    STATEMENT_INSERT,
    STATEMENT_SELECT
};

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
const u_int32_t ROWS_PER_PAGE = PAGE_SIZE / ROW_SIZE;
const u_int32_t TABLE_MAX_ROWS = ROWS_PER_PAGE * TABLE_MAX_PAGES;

struct Pager {
    FILE* fileDescriptor;
    long fileLength;
    void* pages[TABLE_MAX_PAGES];
};

struct Table {
    u_int32_t rowCount;
    Pager* pager;
};

struct Cursor {
    Table* table;
    int rowNum;
    bool endOfTable;
};

// function declarations

void printPrompt();
void readInputBuffer(string& inputBuffer);
MetaCommandResult doMetaCommand(string& inputBuffer, Table* table);
PrepareResult prepareStatement(string inputBuffer, Statement* statement);
ExecuteResult executeStatement(Statement* statement, Table* table);
ExecuteResult executeInsert(Statement* statement, Table* table);
ExecuteResult executeSelect(Statement* statement, Table* table);
void printRow(Row* row);
void serializeRow(Row* src, void* dest);
void deserializeRow(void* src, Row* dest);
void* cursorValue(Cursor* cursor);
Table* openDB(const char* filename);
void pagerFlush(Pager* pager, int pageNum, int size);
long getFileSize(const char* filename);
Pager* pagerOpen(const char* filename);
void* getPage(Pager* pager, int pageNum);
void dbClose(Table* table);
Cursor* tableStart(Table* table);
Cursor* tableEnd(Table* table);
void cursorAdvance(Cursor* cursor);


int main(int argc, char* argv[]) {
    if(argc < 2) {
        cerr << "Must supply a database filename\n";
        exit(EXIT_FAILURE);
    }
    const char* filename = argv[1];
    Table* table = openDB(filename);
    string inputBuffer = "";
    while(true) {
        cout << endl;

        printPrompt();
        readInputBuffer(inputBuffer);
        
        if(inputBuffer[0] == '.') {
            switch (doMetaCommand(inputBuffer, table))
            {
                case (MetaCommandResult::META_COMMAND_SUCCESS):
                    continue;
                case (MetaCommandResult::META_COMMAND_UNRECOGNIZED_COMMAND):
                    cout << "\nUnrecognized meta command '" << inputBuffer <<  "'" << endl;
                    continue;
            }
        }

        Statement statement;
        switch (prepareStatement(inputBuffer, &statement))
        {
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
                cout << "\nUnrecognized keyword at the start of '" << inputBuffer << "'"<< endl;
                continue;
        }

        switch(executeStatement(&statement, table)) {
            case ExecuteResult::EXECUTE_SUCCESS:
                break;
            case ExecuteResult::EXECUTE_TABLE_FULL:
                cout << "\nError: Table full! Meow" << endl;
                break;
        }
    }
}

void printPrompt() {
    cout << "^_^ : ";
}

void readInputBuffer(string& inputBuffer) {
    getline(cin, inputBuffer);

    if (inputBuffer.length() <= 0) {
        cout << "\nError reading input\n";
        exit(EXIT_FAILURE);
    }
}

MetaCommandResult doMetaCommand(string& inputBuffer, Table* table) {
    if (inputBuffer == ".exit") {
        dbClose(table);
        cout << "Exiting..." << endl;
        cout << "Meowwww angyyyyy!" << endl;
        cout << "😾😾😾😾😾\n";
        exit(EXIT_SUCCESS);
    } else {
        return MetaCommandResult::META_COMMAND_UNRECOGNIZED_COMMAND;
    }

}

PrepareResult prepareStatement(string inputBuffer, Statement* statement) {
    int id;
    char username[COLUMN_USERNAME_SIZE];
    char email[COLUMN_EMAIL_SIZE];
    string command;
    stringstream ss(inputBuffer);
    ss >> command;

    if (command == "insert") {
        statement->type = StatementType::STATEMENT_INSERT;
        if (ss >> id >> username >> email) {
            if ((strlen(username) > COLUMN_USERNAME_SIZE) || (strlen(email) > COLUMN_EMAIL_SIZE)) {
                return PrepareResult::PREPARE_STRING_TOO_LONG;
            }

            if(id < 0) {
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

ExecuteResult executeInsert(Statement* statement, Table* table) {
    if (table->rowCount >= TABLE_MAX_ROWS) {
        return ExecuteResult::EXECUTE_TABLE_FULL;
    }
    
    Row* rowToInsert = &(statement->row_to_insert);
    Cursor* cursor = tableEnd(table);

    serializeRow(rowToInsert, cursorValue(cursor));
    table->rowCount++; 

    cout << endl;
    cout << "Inserted 1 row succesfully!" << endl;

    delete cursor;

    return ExecuteResult::EXECUTE_SUCCESS;
}

ExecuteResult executeSelect(Statement* statement, Table* table) {
    Cursor* cursor = tableStart(table);
    Row row;

    cout << endl;

    for (u_int32_t i = 0; i < table->rowCount; i++) {
        deserializeRow(cursorValue(cursor), &row);
        printRow(&row);
    }
    cout << endl;
    cout << table->rowCount << " rows projected!" << endl;

    delete cursor;

    return ExecuteResult::EXECUTE_SUCCESS;
}

void printRow(Row *row)
{
    cout << "(" << row->id << ", " << row->username << ", " << row->email << ")" << endl;
}

ExecuteResult executeStatement(Statement* statement, Table* table) {
    switch (statement->type)
    {
        case StatementType::STATEMENT_INSERT:
            return executeInsert(statement, table);
        case StatementType::STATEMENT_SELECT:
            return executeSelect(statement, table);
        default:
            return ExecuteResult::EXECUTE_UNSUPPORTED_COMMAND;
    }
}

void serializeRow(Row* src, void* dest) {
    char* destination = static_cast<char*>(dest);

    memcpy(destination + ID_OFFSET, &(src->id), ID_SIZE);
    memcpy(destination + USERNAME_OFFSET, &(src->username), USERNAME_SIZE);
    memcpy(destination + EMAIL_OFFSET, &(src->email), EMAIL_SIZE);
} 

void deserializeRow(void* src, Row* dest) {
    const char* source = static_cast<const char*>(src);
    
    memcpy(&(dest->id), source + ID_OFFSET, ID_SIZE);
    memcpy(&(dest->username), source + USERNAME_OFFSET, USERNAME_SIZE); 
    memcpy(&(dest->email), source + EMAIL_OFFSET, EMAIL_SIZE);
}

void* cursorValue(Cursor* cursor) {
    u_int32_t pageNum = cursor->rowNum / ROWS_PER_PAGE;
    void* page = getPage(cursor->table->pager, pageNum);
    u_int32_t rowOffset = cursor->rowNum % ROWS_PER_PAGE;
    u_int32_t byteOffset = rowOffset * ROW_SIZE;

    return static_cast<char*>(page) + byteOffset;
}

void* getPage(Pager* pager, int pageNum) {
    if(pageNum > TABLE_MAX_PAGES) {
        cout << "Tried to fetch page number out of bounds. " << pageNum << " > " << TABLE_MAX_PAGES << endl;
        exit(EXIT_FAILURE);
    }
    
    if(pager->pages[pageNum] == nullptr) {
        // cache miss. Allocate memory and load from file
        void* page = new char[PAGE_SIZE]();
        int numPages = pager->fileLength / PAGE_SIZE;

        // We might save a partial page at the end of the file
        if(pager->fileLength % PAGE_SIZE) {
            numPages++;
        }

        if(pageNum <= numPages) {
            fseek(pager->fileDescriptor, pageNum * PAGE_SIZE, SEEK_SET);
            int bytesRead = fread(page, 1, pager->fileLength, pager->fileDescriptor);
        }
        pager->pages[pageNum] = page;
    }

    return pager->pages[pageNum];

}

Table* openDB(const char* filename)
{
    Pager* pager = pagerOpen(filename);
    int rowCount = pager->fileLength / ROW_SIZE;

    Table* table = new Table();

    table->pager = pager;
    table->rowCount = rowCount;

    return table;
}

void dbClose(Table* table) {
    Pager* pager = table->pager;
    int numFullPages = table->rowCount / ROWS_PER_PAGE;

    for(int i = 0; i < numFullPages; i++) {
        if(pager->pages[i] == nullptr) {
            continue;
        }
        pagerFlush(pager, i, PAGE_SIZE);
        delete[] static_cast<char*>(pager->pages[i]);
        pager->pages[i] = nullptr;
    }

    int additionalRows = table->rowCount % ROWS_PER_PAGE;
    if(additionalRows > 0) {
        int pageNum = numFullPages;
        if(pager->pages[pageNum] != nullptr) {
            pagerFlush(pager, pageNum, additionalRows * ROW_SIZE);
            delete[] static_cast<char*>(pager->pages[pageNum]);
            pager->pages[pageNum] = nullptr;
        }
    }

    int result = fclose(pager->fileDescriptor);
    if(result == -1) {
        cout << "Error closing db file\n";
        exit(EXIT_FAILURE);
    }

    for(int i = 0; i < TABLE_MAX_PAGES; i++) {
        void* page = pager->pages[i];
        if(page) {
            delete static_cast<char*>(page);
            pager->pages[i] = nullptr;
        }
    }

    delete pager;    
}

Pager* pagerOpen(const char* filename) {
    FILE* file = fopen(filename, "r+b");

    if(file == nullptr) {
        cout << "Unable to open file\n";
        exit(EXIT_FAILURE);
    }

    Pager *pager = new Pager();
    pager->fileDescriptor = file;
    pager->fileLength = getFileSize(filename);
    
    for(int i = 0; i < TABLE_MAX_PAGES; i++) {
        pager->pages[i] = nullptr;
    }

    return pager;
}

long getFileSize(const char* filename) {
    FILE* fp = fopen(filename, "rb");
    if(fp == nullptr) {
        cout << "Unable to open file\n";
        exit(EXIT_FAILURE);
    }

    if(fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }

    long size = ftell(fp);

    fclose(fp);

    return size;
}

void pagerFlush(Pager* pager, int pageNum, int size) {
    if(pager->pages[pageNum] == nullptr) {
        cout << "Tried to flush null\n";
        exit(EXIT_FAILURE);
    }

    fseek(pager->fileDescriptor, pageNum * PAGE_SIZE, SEEK_SET);

    int bytesWritten = fwrite(pager->pages[pageNum], 1, size, pager->fileDescriptor);
}

Cursor* tableStart(Table* table) {
    Cursor* cursor = new Cursor();
    cursor->table = table;
    cursor->rowNum = 0;
    cursor->endOfTable = (table->rowCount == 0);

    return cursor;
}

Cursor* tableEnd(Table* table) {
    Cursor* cursor = new Cursor();
    cursor->table = table;
    cursor->rowNum = table->rowCount;
    cursor->endOfTable = true;

    return cursor;
}

void cursorAdvance(Cursor* cursor) {
    cursor->rowNum++;
    if(cursor->rowNum >= cursor->table->rowCount) {
        cursor->endOfTable = true;
    }
}