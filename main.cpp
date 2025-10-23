// main.cpp
#include "db.hpp"

// --- InputBuffer Functions ---
InputBuffer* new_input_buffer() {
    // 'new' calls the constructor
    return new InputBuffer();
}

void read_input(InputBuffer* input_buffer) {
    // std::getline works perfectly with std::string
    std::getline(std::cin, input_buffer->buffer);

    if (std::cin.fail() && !std::cin.eof()) {
        std::cout << "Error reading input" << std::endl;
        exit(EXIT_FAILURE);
    }
}

void close_input_buffer(InputBuffer* input_buffer) {
    // 'delete' calls the destructor
    // The std::string in InputBuffer is automatically freed
    delete input_buffer;
}

// --- Table/Pager Functions (NEW DAY 5) ---
Table* new_table(const std::string& filename) {
    Table* table = new Table();

    // Open the file in binary mode for both reading and writing
    table->file_stream.open(filename,
                            std::ios::in | std::ios::out | std::ios::binary);

    // If the file doesn't exist, the 'in' flag makes it fail.
    // So, we re-open with 'trunc' which creates the file.
    if (!table->file_stream.is_open()) {
        table->file_stream.open(filename,
                                std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    }

    if (!table->file_stream.is_open()) {
        std::cout << "Failed to open database file: " << filename << std::endl;
        exit(EXIT_FAILURE);
    }

    // Go to the end of the file to get its length
    table->file_stream.seekg(0, std::ios::end);
    uint32_t file_length = table->file_stream.tellg();

    // Calculate the number of rows from the file size
    table->num_rows = file_length / ROW_SIZE;

    return table;
}

void free_table(Table* table) {
    // Close the file stream if it's open
    if (table->file_stream.is_open()) {
        table->file_stream.close();
    }
    // Free the table struct
    delete table;
}

// --- Meta-Command Function ---
MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table* table) {
  // We can compare std::string directly
  if (input_buffer->buffer == ".exit") {
    close_input_buffer(input_buffer);
    free_table(table);
    exit(EXIT_SUCCESS);
  } else {
    return META_COMMAND_UNRECOGNIZED_COMMAND;
  }
}

// --- Parser Function ---
PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement) {

    // Use .rfind() to check if string starts with "insert"
    if (input_buffer->buffer.rfind("insert", 0) == 0) {
        statement->type = STATEMENT_INSERT;

        // Use std::stringstream to parse the line, C++ style
        std::stringstream ss(input_buffer->buffer);
        std::string command; // To consume the "insert" part

        // Try to stream data into our struct members
        ss >> command
           >> statement->row_to_insert.id
           >> statement->row_to_insert.username
           >> statement->row_to_insert.email;

        // Check if the stream failed (e.g., not enough args)
        if (ss.fail()) {
            return PREPARE_SYNTAX_ERROR;
        }

        // Check for username/email length
        if (strlen(statement->row_to_insert.username) > COLUMN_USERNAME_SIZE ||
            strlen(statement->row_to_insert.email) > COLUMN_EMAIL_SIZE) {
            std::cout << "Error: String is too long." << std::endl;
            return PREPARE_SYNTAX_ERROR;
        }

        return PREPARE_SUCCESS;
    }

    // Check for 'select'
    if (input_buffer->buffer == "select") {
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }

    return PREPARE_UNRECOGNIZED_STATEMENT;
}

// --- Storage/Serialization Functions ---
void serialize_row(Row* source, void* destination) {
    memcpy(static_cast<char*>(destination) + ID_OFFSET, &(source->id), ID_SIZE);
    strncpy(static_cast<char*>(destination) + USERNAME_OFFSET, source->username, USERNAME_SIZE);
    strncpy(static_cast<char*>(destination) + EMAIL_OFFSET, source->email, EMAIL_SIZE);
}

void deserialize_row(void* source, Row* destination) {
    memcpy(&(destination->id), static_cast<char*>(source) + ID_OFFSET, ID_SIZE);
    strncpy(destination->username, static_cast<char*>(source) + USERNAME_OFFSET, USERNAME_SIZE);
    destination->username[USERNAME_SIZE] = '\0'; // Ensure termination
    strncpy(destination->email, static_cast<char*>(source) + EMAIL_OFFSET, EMAIL_SIZE);
    destination->email[EMAIL_SIZE] = '\0'; // Ensure termination
}

// --- row_slot() function removed ---


// Helper function to print a row
void print_row(Row* row) {
    std::cout << "(" << row->id << ", "
              << row->username << ", "
              << row->email << ")" << std::endl;
}

// --- Executor Functions (NEW DAY 5) ---
ExecuteResult execute_insert(Statement* statement, Table* table) {
    if (table->num_rows >= TABLE_MAX_ROWS) {
        return EXECUTE_TABLE_FULL;
    }

    Row* row_to_insert = &(statement->row_to_insert);

    // Create a stack-allocated buffer for one row
    char buffer[ROW_SIZE];

    // Serialize the row into the buffer
    serialize_row(row_to_insert, buffer);

    // Find the file offset (end of the file)
    uint32_t offset = table->num_rows * ROW_SIZE;

    // Seek to that offset
    table->file_stream.seekp(offset);

    // Write the serialized row
    table->file_stream.write(buffer, ROW_SIZE);

    // Check for write errors
    if (!table->file_stream) {
        std::cout << "Error: Failed to write to database file." << std::endl;
        exit(EXIT_FAILURE);
    }

    table->num_rows += 1;

    return EXECUTE_SUCCESS;
}

ExecuteResult execute_select(Statement* statement, Table* table) {
    Row row; // A stack-allocated Row struct to deserialize into
    char buffer[ROW_SIZE]; // A buffer to read the row from disk

    // Loop through all saved rows
    for (uint32_t i = 0; i < table->num_rows; i++) {
        // Find the offset for the i-th row
        uint32_t offset = i * ROW_SIZE;

        // Seek to that offset
        table->file_stream.seekg(offset);

        // Read the row into the buffer
        table->file_stream.read(buffer, ROW_SIZE);

        // Deserialize the bytes from the buffer into our row struct
        deserialize_row(buffer, &row);

        // Print the row's contents
        print_row(&row);
    }

    return EXECUTE_SUCCESS;
}

ExecuteResult execute_statement(Statement* statement, Table* table) {
  switch (statement->type) {
    case (STATEMENT_INSERT):
      return execute_insert(statement, table);
    case (STATEMENT_SELECT):
          return execute_select(statement, table);
  }
  return EXECUTE_SUCCESS; // Should be unreachable
}



// --- REPL (Main) (NEW DAY 5) ---
void print_prompt() { std::cout << "db > "; }

int main(int argc, char* argv[]) {
    // Check for the database filename
    if (argc < 2) {
        std::cout << "Must supply a database filename." << std::endl;
        exit(EXIT_FAILURE);
    }

    std::string filename = argv[1];
    Table* table = new_table(filename); // Pass filename to new_table
    InputBuffer* input_buffer = new_input_buffer();

    while (true) {
        print_prompt();
        read_input(input_buffer);

        if (input_buffer->buffer.empty()) {
            continue; // Skip empty input
        }

        if (input_buffer->buffer[0] == '.') {
            switch (do_meta_command(input_buffer, table)) {
                case (META_COMMAND_SUCCESS):
                    continue;
                case (META_COMMAND_UNRECOGNIZED_COMMAND):
                    std::cout << "Unrecognized command '" << input_buffer->buffer << "'" << std::endl;
                    continue;
            }
        }

        Statement statement;
        switch (prepare_statement(input_buffer, &statement)) {
            case (PREPARE_SUCCESS):
                break;
            case (PREPARE_SYNTAX_ERROR):
                std::cout << "Syntax error. Could not parse statement." << std::endl;
                continue;
            case (PREPARE_UNRECOGNIZED_STATEMENT):
                std::cout << "Unrecognized keyword at start of '" << input_buffer->buffer << "'." << std::endl;
                continue;
        }

        switch (execute_statement(&statement, table)) {
            case (EXECUTE_SUCCESS):
                std::cout << "Executed." << std::endl;
                break;
            case (EXECUTE_TABLE_FULL):
                std::cout << "Error: Table full." << std::endl;
                break;
        }
    }
}