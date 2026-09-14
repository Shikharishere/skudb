// skuDB - skuba db 🐈

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <string>

using namespace std;

enum class MetaCommandResult {
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNIZED_COMMAND
};

enum class PrepareResult {
    PREPARE_SUCCESS,
    PREPARE_UNRECOGNIZED_STATEMENT
};

enum class StatementType {
    STATEMENT_INSERT,
    STATEMENT_SELECT
};

struct Statement {
    StatementType type;
};

// function declarations

void printPrompt();
void readInputBuffer(string& inputBuffer);
MetaCommandResult doMetaCommand(string& inputBuffer);
PrepareResult prepareStatement(string inputBuffer, Statement* statement);
void execute(Statement* statement);

int main() {
    string inputBuffer = "";
    while(true) {
        cout << endl;

        printPrompt();
        readInputBuffer(inputBuffer);
        
        if(inputBuffer[0] == '.') {
            switch (doMetaCommand(inputBuffer))
            {
                case (MetaCommandResult::META_COMMAND_SUCCESS):
                    continue;
                case (MetaCommandResult::META_COMMAND_UNRECOGNIZED_COMMAND):
                    cout << "Unrecognized meta command '" << inputBuffer <<  "'" << endl;
                    continue;
            }
        }

        Statement statement;
        switch (prepareStatement(inputBuffer, &statement))
        {
            case (PrepareResult::PREPARE_SUCCESS):
                break;
            case (PrepareResult::PREPARE_UNRECOGNIZED_STATEMENT):
                cout << "Unrecognized keyword at the start of '" << inputBuffer << "'"<< endl;
                continue;
        }

        execute(&statement);
        cout << "Executed\n";
    }
}

void printPrompt() {
    cout << "^_^ : ";
}

void readInputBuffer(string& inputBuffer) {
    getline(cin, inputBuffer);

    if(inputBuffer.length() <= 0) {
        cout << "Error reading input\n";
        exit(EXIT_FAILURE);
    }
}

MetaCommandResult doMetaCommand(string& inputBuffer) {
    if(inputBuffer == ".exit") {
        exit(EXIT_SUCCESS);
    } else {
        return MetaCommandResult::META_COMMAND_UNRECOGNIZED_COMMAND;
    }

}

PrepareResult prepareStatement(string inputBuffer, Statement* statement) {
    if(inputBuffer.substr(0, 6) == "insert") {
        statement->type = StatementType::STATEMENT_INSERT;
        return PrepareResult::PREPARE_SUCCESS;
    } else if(inputBuffer.substr(0, 6) == "select") {
        statement->type = StatementType::STATEMENT_SELECT;
        return PrepareResult::PREPARE_SUCCESS;
    }

    return PrepareResult::PREPARE_UNRECOGNIZED_STATEMENT;
}

void execute(Statement* statement) {
    switch (statement->type)
    {
        case (StatementType::STATEMENT_INSERT):
            cout << "Damn! An insert statement 0_0\n";
            break;
        case (StatementType::STATEMENT_SELECT):
            cout << "Select statement yayyyyyy meowwwwwww\n";
    }
}