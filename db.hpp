// db.hpp - Enhanced Version with Persistence Fix
#ifndef DB_HPP
#define DB_HPP

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <fstream>
#include <iomanip>

// --- Constants ---
#define DB_FILE_HEADER_SIZE 8 // root_page_num (4 bytes) + free_head (4 bytes)

// --- Data Structures ---

typedef struct {
  std::string buffer;
} InputBuffer;

// Row Schema
#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255
typedef struct {
  uint32_t id;
  char username[COLUMN_USERNAME_SIZE + 1];
  char email[COLUMN_EMAIL_SIZE + 1];
} Row;

// Statement
typedef enum {
    STATEMENT_INSERT,
    STATEMENT_SELECT,
    STATEMENT_FIND,
    STATEMENT_DELETE,
    STATEMENT_UPDATE,
    STATEMENT_RANGE
} StatementType;

typedef struct {
  StatementType type;
  Row row_to_insert;
  uint32_t range_start;
  uint32_t range_end;
} Statement;

// Table Structure constants
#define PAGE_SIZE 4096
#define TABLE_MAX_PAGES 100000 // Increased to support very large databases
#define ID_SIZE sizeof(uint32_t)
#define USERNAME_SIZE COLUMN_USERNAME_SIZE
#define EMAIL_SIZE COLUMN_EMAIL_SIZE
#define ID_OFFSET 0
#define USERNAME_OFFSET (ID_OFFSET + ID_SIZE)
#define EMAIL_OFFSET (USERNAME_OFFSET + USERNAME_SIZE)
#define ROW_SIZE (ID_SIZE + USERNAME_SIZE + EMAIL_SIZE)
#define ROWS_PER_PAGE (PAGE_SIZE / ROW_SIZE)
#define TABLE_MAX_ROWS (ROWS_PER_PAGE * TABLE_MAX_PAGES)

// --- PAGER STRUCT ---
typedef struct {
  std::fstream file_stream;
  uint32_t file_length;
  void* pages[TABLE_MAX_PAGES];
  uint32_t num_pages;
  uint32_t root_page_num; // <<<--- MOVED HERE FROM TABLE
  uint32_t free_head;     // Head of free page list (0 = no free pages)
} Pager;

// --- TABLE STRUCT ---
typedef struct {
  // uint32_t root_page_num; // <<<--- REMOVED FROM HERE
  Pager* pager;
} Table;

// --- CURSOR STRUCT ---
typedef struct {
    Table* table;
    uint32_t page_num;
    uint32_t cell_num;
    bool end_of_table;
} Cursor;

// --- Enums for Results  ---

typedef enum {
  META_COMMAND_SUCCESS,
  META_COMMAND_UNRECOGNIZED_COMMAND
} MetaCommandResult;

typedef enum {
  PREPARE_SUCCESS,
  PREPARE_SYNTAX_ERROR,
  PREPARE_UNRECOGNIZED_STATEMENT,
  PREPARE_STRING_TOO_LONG
} PrepareResult;

typedef enum {
  EXECUTE_SUCCESS,
  EXECUTE_TABLE_FULL,
  EXECUTE_DUPLICATE_KEY,
  EXECUTE_RECORD_NOT_FOUND,
  EXECUTE_DISK_ERROR,
  EXECUTE_PAGE_OUT_OF_BOUNDS
} ExecuteResult;

// Pager-specific result codes
typedef enum {
  PAGER_SUCCESS,
  PAGER_DISK_ERROR,
  PAGER_OUT_OF_BOUNDS,
  PAGER_CORRUPT_FILE,
  PAGER_NULL_PAGE
} PagerResult;

// --- B-TREE NODE CONSTANTS ---

typedef enum { NODE_INTERNAL, NODE_LEAF } NodeType;

// Common Node Header Layout
#define NODE_TYPE_SIZE sizeof(uint8_t)
#define NODE_TYPE_OFFSET 0
#define IS_ROOT_SIZE sizeof(uint8_t)
#define IS_ROOT_OFFSET NODE_TYPE_SIZE
#define PARENT_POINTER_SIZE sizeof(uint32_t)
#define PARENT_POINTER_OFFSET (IS_ROOT_OFFSET + IS_ROOT_SIZE)
#define COMMON_NODE_HEADER_SIZE (NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE)

// Leaf Node Header Layout - ENHANCED with sibling pointer
#define LEAF_NODE_NUM_CELLS_SIZE sizeof(uint32_t)
#define LEAF_NODE_NUM_CELLS_OFFSET COMMON_NODE_HEADER_SIZE
#define LEAF_NODE_NEXT_LEAF_SIZE sizeof(uint32_t)
#define LEAF_NODE_NEXT_LEAF_OFFSET (LEAF_NODE_NUM_CELLS_OFFSET + LEAF_NODE_NUM_CELLS_SIZE)
#define LEAF_NODE_HEADER_SIZE (COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELLS_SIZE + LEAF_NODE_NEXT_LEAF_SIZE)

// Leaf Node Body Layout
#define LEAF_NODE_KEY_SIZE sizeof(uint32_t)
#define LEAF_NODE_VALUE_SIZE ROW_SIZE
#define LEAF_NODE_CELL_SIZE (LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE)
#define LEAF_NODE_SPACE_FOR_CELLS (PAGE_SIZE - LEAF_NODE_HEADER_SIZE)
#define LEAF_NODE_MAX_CELLS (LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE)
#define LEAF_NODE_MIN_CELLS (LEAF_NODE_MAX_CELLS / 2) // Minimum cells for rebalancing

// Internal Node Header Layout
#define INTERNAL_NODE_NUM_KEYS_SIZE sizeof(uint32_t)
#define INTERNAL_NODE_NUM_KEYS_OFFSET COMMON_NODE_HEADER_SIZE
#define INTERNAL_NODE_RIGHT_CHILD_SIZE sizeof(uint32_t)
#define INTERNAL_NODE_RIGHT_CHILD_OFFSET (INTERNAL_NODE_NUM_KEYS_OFFSET + INTERNAL_NODE_NUM_KEYS_SIZE)
#define INTERNAL_NODE_HEADER_SIZE (COMMON_NODE_HEADER_SIZE + INTERNAL_NODE_NUM_KEYS_SIZE + INTERNAL_NODE_RIGHT_CHILD_SIZE)

// Internal Node Body Layout
#define INTERNAL_NODE_KEY_SIZE sizeof(uint32_t)
#define INTERNAL_NODE_CHILD_SIZE sizeof(uint32_t)
#define INTERNAL_NODE_CELL_SIZE (INTERNAL_NODE_CHILD_SIZE + INTERNAL_NODE_KEY_SIZE)
#define INTERNAL_NODE_SPACE_FOR_CELLS (PAGE_SIZE - INTERNAL_NODE_HEADER_SIZE)
#define INTERNAL_NODE_MAX_KEYS (INTERNAL_NODE_SPACE_FOR_CELLS / INTERNAL_NODE_CELL_SIZE)
#define INTERNAL_NODE_MIN_KEYS (INTERNAL_NODE_MAX_KEYS / 2) // Minimum keys for rebalancing

// --- Function Prototypes ---

// InputBuffer functions
InputBuffer* new_input_buffer();
void read_input(InputBuffer* input_buffer);
void close_input_buffer(InputBuffer* input_buffer);

// Meta-command function
MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table* table);

// Parser function
PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement);

// Storage/Serialization functions
void serialize_row(Row* source, void* destination);
void deserialize_row(void* source, Row* destination);

// Pager functions
Pager* pager_open(const std::string& filename);
void* pager_get_page(Pager* pager, uint32_t page_num);
PagerResult pager_flush(Pager* pager, uint32_t page_num);
uint32_t get_unused_page_num(Pager* pager);
void free_page(Pager* pager, uint32_t page_num);

// B-Tree Node Accessor functions
uint32_t* get_leaf_node_num_cells(void* node);
void* get_leaf_node_cell(void* node, uint32_t cell_num);
uint32_t* get_leaf_node_key(void* node, uint32_t cell_num);
void* get_leaf_node_value(void* node, uint32_t cell_num);
uint32_t* get_leaf_node_next_leaf(void* node);
void initialize_leaf_node(void* node);
NodeType get_node_type(void* node);
void set_node_type(void* node, NodeType type);
bool is_node_root(void* node);
void set_node_root(void* node, bool is_root);
uint32_t* get_node_parent(void* node);
void initialize_internal_node(void* node);
uint32_t* get_internal_node_num_keys(void* node);
uint32_t* get_internal_node_right_child(void* node);
uint32_t* get_internal_node_child(void* node, uint32_t child_num);
uint32_t* get_internal_node_key(void* node, uint32_t key_num);
uint32_t get_node_max_key(Pager* pager, void* node);

// Table functions
Table* new_table(const std::string& filename);
void free_table(Table* table);
Cursor* table_start(Table* table);
Cursor* table_find(Table* table, uint32_t key);
Cursor* leaf_node_find(Table* table, uint32_t page_num, uint32_t key);
void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value);
void create_new_root(Table* table, uint32_t right_child_page_num);
void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, Row* value);
void leaf_node_delete(Cursor* cursor);
void handle_leaf_underflow(Table* table, uint32_t page_num);
void handle_internal_underflow(Table* table, uint32_t page_num);
void update_internal_node_key(void* node, uint32_t old_max, uint32_t new_max);

// Executor functions
ExecuteResult execute_statement(Statement* statement, Table* table);
ExecuteResult execute_insert(Statement* statement, Table* table);
ExecuteResult execute_select(Statement* statement, Table* table);
ExecuteResult execute_find(Statement* statement, Table* table);
ExecuteResult execute_delete(Statement* statement, Table* table);
ExecuteResult execute_update(Statement* statement, Table* table);
ExecuteResult execute_range(Statement* statement, Table* table);

// REPL helper
void print_prompt();
void print_row(Row* row);

// Visualization functions
void print_tree(Table* table);
void print_node(Pager* pager, uint32_t page_num, uint32_t indent_level);

// Validation functions
void validate_tree(Table* table);
bool validate_tree_node(Pager* pager, uint32_t page_num, uint32_t* min_key, uint32_t* max_key, int* depth, bool is_root_call);

#endif //DB_HPP