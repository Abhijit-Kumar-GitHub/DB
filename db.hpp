// db.hpp
#ifndef DB_HPP
#define DB_HPP

#include <cstdint>  // For uint32_t
#include <iostream> // For std::cout, std::cin
#include <string>   // For std::string
#include <vector>   // For std::vector
#include <cstdlib>  // For exit
#include <cstring>  // For memcpy, strncpy
#include <sstream>  // For std::stringstream
#include <fstream>

// --- Data Structures ---

// InputBuffer now uses std::string
typedef struct {
  std::string buffer;
} InputBuffer;

// Row Schema (KEPT AS C-STYLE STRUCT for layout control)
#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255
typedef struct {
  uint32_t id;
  char username[COLUMN_USERNAME_SIZE + 1];
  char email[COLUMN_EMAIL_SIZE + 1];
} Row;

// Statement (unchanged)
typedef enum { STATEMENT_INSERT, STATEMENT_SELECT } StatementType;

typedef struct {
  StatementType type;
  Row row_to_insert; // Only used by insert
} Statement;

// Table Structure constants (unchanged)
#define PAGE_SIZE 4096
#define TABLE_MAX_PAGES 100
#define ID_SIZE sizeof(uint32_t)
#define USERNAME_SIZE COLUMN_USERNAME_SIZE
#define EMAIL_SIZE COLUMN_EMAIL_SIZE
#define ID_OFFSET 0
#define USERNAME_OFFSET (ID_OFFSET + ID_SIZE)
#define EMAIL_OFFSET (USERNAME_OFFSET + USERNAME_SIZE)
#define ROW_SIZE (ID_SIZE + USERNAME_SIZE + EMAIL_SIZE)
#define ROWS_PER_PAGE (PAGE_SIZE / ROW_SIZE)
#define TABLE_MAX_ROWS (ROWS_PER_PAGE * TABLE_MAX_PAGES)

// Table struct now uses std::vector
typedef struct {
  uint32_t num_rows;
  std::fstream file_stream;
  void* pages[TABLE_MAX_PAGES];
} Table;


// --- Enums for Results (unchanged) ---

typedef enum {
  META_COMMAND_SUCCESS,
  META_COMMAND_UNRECOGNIZED_COMMAND
} MetaCommandResult;

typedef enum {
  PREPARE_SUCCESS,
  PREPARE_SYNTAX_ERROR,
  PREPARE_UNRECOGNIZED_STATEMENT
} PrepareResult;

typedef enum {
  EXECUTE_SUCCESS,
  EXECUTE_TABLE_FULL
} ExecuteResult;


// --- Function Prototypes ---

// InputBuffer functions
InputBuffer* new_input_buffer();
void read_input(InputBuffer* input_buffer);
void close_input_buffer(InputBuffer* input_buffer);

// Meta-command function
MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table* table);

// Parser function
PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement);

// Storage/Serialization functions (prototypes unchanged)
void serialize_row(Row* source, void* destination);
void deserialize_row(void* source, Row* destination);

// Table/Pager functions
Table* new_table(const std::string& filename);
void free_table(Table* table);

// Executor functions
ExecuteResult execute_statement(Statement* statement, Table* table);
ExecuteResult execute_insert(Statement* statement, Table* table);
ExecuteResult execute_select(Statement* statement, Table* table);

// REPL helper
void print_prompt();
void print_row(Row* row);


#endif //DB_HPP