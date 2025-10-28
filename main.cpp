// main.cpp - Enhanced Version with Persistence Fix

/*
 * TABLE OF CONTENTS:
 * 1. InputBuffer Functions (line 19)
 * 2. Pager Functions (line 36)
 * 3. Table Functions (line 169)
 * 4. Meta-Command Function (line 245)
 * 4. B-Tree Node Accessors (line )
 * 5. B-Tree Operations (line )
 * 6. Executor Functions (line )
 * 7. Visualization (line )
 * 8. Main REPL (line )
 */

#include "db.hpp"

using namespace std;

// --- InputBuffer Functions ---
InputBuffer* new_input_buffer() {
    return new InputBuffer();
}

void read_input(InputBuffer* input_buffer) {
    getline(cin, input_buffer->buffer);
    if (cin.fail() && !cin.eof()) {
        cout << "Error reading input" << endl;
        exit(EXIT_FAILURE);
    }
}

void close_input_buffer(InputBuffer* input_buffer) {
    delete input_buffer;
}

// --- Pager Functions ---
Pager* pager_open(const string& filename) {
    Pager* pager = new Pager();

    pager->file_stream.open(filename, ios::in | ios::out | ios::binary);

    if (!pager->file_stream.is_open()) {
        // File doesn't exist, create it
        pager->file_stream.open(filename, ios::in | ios::out | ios::binary | ios::trunc);
        if (!pager->file_stream.is_open()) {
             cout << "Failed to create database file: " << filename << endl;
             exit(EXIT_FAILURE);
        }
        // Initialize header for new file
        pager->root_page_num = 0; // Root starts at page 0
        pager->num_pages = 1;     // Start with one page (the root)
        pager->file_length = PAGE_SIZE + DB_FILE_HEADER_SIZE; // Header + 1 page

        // Write the initial header
        pager->file_stream.seekp(0);
        pager->file_stream.write(reinterpret_cast<char*>(&pager->root_page_num), sizeof(pager->root_page_num));
        // Ensure file is large enough for header + initial page
        char zero_page[PAGE_SIZE] = {0};
        pager->file_stream.seekp(DB_FILE_HEADER_SIZE);
        pager->file_stream.write(zero_page, PAGE_SIZE);
        pager->file_stream.flush(); // Make sure header is written
        
        // Note: The root page will be properly initialized in new_table()

    } else {
        // File exists, read header
        pager->file_stream.seekg(0);
        pager->file_stream.read(reinterpret_cast<char*>(&pager->root_page_num), sizeof(pager->root_page_num));
        if (pager->file_stream.fail()) {
            cout << "Error reading database file header: " << filename << endl;
            exit(EXIT_FAILURE);
        }

        // Calculate file length and page count based on actual size
        pager->file_stream.seekg(0, ios::end);
        pager->file_length = pager->file_stream.tellg();

        // Validate file length
        if (pager->file_length < DB_FILE_HEADER_SIZE || (pager->file_length - DB_FILE_HEADER_SIZE) % PAGE_SIZE != 0) {
            cout << "Error: Corrupt database file. Invalid size." << endl;
            exit(EXIT_FAILURE);
        }
        pager->num_pages = (pager->file_length - DB_FILE_HEADER_SIZE) / PAGE_SIZE;
    }


    for (int i = 0; i < TABLE_MAX_PAGES; i++) {
        pager->pages[i] = nullptr;
    }
    return pager;
}

// Calculates the byte offset for a given page number in the file
uint64_t get_page_file_offset(uint32_t page_num) {
    return DB_FILE_HEADER_SIZE + (uint64_t)page_num * PAGE_SIZE;
}


void* pager_get_page(Pager* pager, uint32_t page_num) {
    if (page_num >= TABLE_MAX_PAGES) {
        cout << "Error: Tried to access page number out of bounds: " << page_num << endl;
        exit(EXIT_FAILURE);
    }

    if (pager->pages[page_num] != nullptr) {
        return pager->pages[page_num];
    }

    void* page = new char[PAGE_SIZE];
    memset(page, 0, PAGE_SIZE);

    // If page_num is beyond the current number of pages, it's a new page
    // We update num_pages when we allocate, not just read
    uint32_t current_num_pages = (pager->file_length - DB_FILE_HEADER_SIZE) / PAGE_SIZE;
    if (page_num >= current_num_pages) {
         // This is a new page being allocated logically
         // num_pages gets updated when the page is flushed if it increases file size
    } else {
        // Page exists in file, read it from disk
        pager->file_stream.seekg(get_page_file_offset(page_num));
        pager->file_stream.read(static_cast<char*>(page), PAGE_SIZE);

        if (pager->file_stream.fail() && !pager->file_stream.eof()) {
            cout << "Error reading page " << page_num << " from file." << endl;
            exit(EXIT_FAILURE);
        }
        // Clear fail bits if we only hit EOF (reading partial last page is ok conceptually, though our format assumes full pages)
        if (pager->file_stream.eof()) {
            pager->file_stream.clear();
        }
    }

    pager->pages[page_num] = page;
    // Update num_pages if this is the highest page number seen so far
    if (page_num >= pager->num_pages) {
         pager->num_pages = page_num + 1;
    }

    return page;
}

void pager_flush(Pager* pager, uint32_t page_num) {
    if (pager->pages[page_num] == nullptr) {
        cout << "Error: Tried to flush a null page." << endl;
        exit(EXIT_FAILURE);
    }

    pager->file_stream.seekp(get_page_file_offset(page_num));
    pager->file_stream.write(static_cast<char*>(pager->pages[page_num]), PAGE_SIZE);

    if (pager->file_stream.fail()) {
        cout << "Error writing page " << page_num << " to file." << endl;
        exit(EXIT_FAILURE);
    }

    // Ensure file_length is updated if we wrote a new page
    uint64_t expected_length = get_page_file_offset(page_num) + PAGE_SIZE;
    if (expected_length > pager->file_length) {
        pager->file_length = expected_length;
        // Implicitly, pager->num_pages should reflect this, ensured by pager_get_page
    }
}

uint32_t get_unused_page_num(Pager* pager) {
    // Returns the current total number of pages, which will be the index of the next new page
    return pager->num_pages;
}

// --- Table/Pager Functions  ---
Table* new_table(const string& filename) {
    Table* table = new Table();
    Pager* pager = pager_open(filename); // Pager now reads root_page_num
    table->pager = pager;
    
    // Initialize the root page as a leaf node if it hasn't been initialized yet
    void* root_node = pager_get_page(pager, pager->root_page_num);
    
    // Check if the root node needs initialization
    NodeType node_type = get_node_type(root_node);
    bool needs_init = false;
    
    if (node_type == NODE_LEAF) {
        // Verify the leaf node has valid data
        uint32_t num_cells = *get_leaf_node_num_cells(root_node);
        if (num_cells > LEAF_NODE_MAX_CELLS) {
            needs_init = true;
        }
        // Also check if it's marked as root
        if (!is_node_root(root_node)) {
            needs_init = true;
        }
    } else if (node_type == NODE_INTERNAL) {
        // Check if it's a valid internal node
        if (!is_node_root(root_node)) {
            needs_init = true;
        }
        uint32_t num_keys = *get_internal_node_num_keys(root_node);
        if (num_keys == 0 && pager->num_pages == 1) {
            // Internal node with no keys in a single-page DB is invalid
            needs_init = true;
        }
    } else {
        // Invalid node type
        needs_init = true;
    }
    
    if (needs_init) {
        // Initialize root as a leaf
        initialize_leaf_node(root_node);
        set_node_root(root_node, true);
    }

    return table;
}

void free_table(Table* table) {
    Pager* pager = table->pager;

    // Flush all cached pages
    for (int i = 0; i < pager->num_pages; i++) { // Iterate up to known num_pages
        if (pager->pages[i] != nullptr) {
            pager_flush(pager, i);
            delete[] static_cast<char*>(pager->pages[i]);
            pager->pages[i] = nullptr;
        }
    }

     // --- WRITE FINAL HEADER ---
    if (pager->file_stream.is_open()) {
        // Write the final root page number before closing
        pager->file_stream.seekp(0);
        pager->file_stream.write(reinterpret_cast<char*>(&pager->root_page_num), sizeof(pager->root_page_num));
        if (pager->file_stream.fail()) {
             cout << "Error writing final header to database file." << endl;
             // Don't exit here, try to close file anyway
        }
        pager->file_stream.close();
    }


    delete pager;
    delete table;
}

// --- Meta-Command Function ---
// (No changes needed here, uses table->pager->root_page_num implicitly via print_tree)
MetaCommandResult do_meta_command(InputBuffer* input_buffer, Table* table) {
    if (input_buffer->buffer == ".exit") {
        close_input_buffer(input_buffer);
        free_table(table);
        exit(EXIT_SUCCESS);
    } else if (input_buffer->buffer == ".btree") {
        cout << "Tree:" << endl;
        print_tree(table);
        return META_COMMAND_SUCCESS;
    } else if (input_buffer->buffer == ".constants") {
        cout << "Constants:" << endl;
        cout << "ROW_SIZE: " << ROW_SIZE << endl;
        cout << "COMMON_NODE_HEADER_SIZE: " << COMMON_NODE_HEADER_SIZE << endl;
        cout << "LEAF_NODE_HEADER_SIZE: " << LEAF_NODE_HEADER_SIZE << endl;
        cout << "LEAF_NODE_CELL_SIZE: " << LEAF_NODE_CELL_SIZE << endl;
        cout << "LEAF_NODE_SPACE_FOR_CELLS: " << LEAF_NODE_SPACE_FOR_CELLS << endl;
        cout << "LEAF_NODE_MAX_CELLS: " << LEAF_NODE_MAX_CELLS << endl;
        return META_COMMAND_SUCCESS;
    } else if (input_buffer->buffer == ".debug") {
        // ... (debug output remains the same, just uses pager->root_page_num) ...
         cout << "=== DEBUG INFO ===" << endl;
        cout << "Root page num: " << table->pager->root_page_num << endl; // Use pager's root
        cout << "Total pages: " << table->pager->num_pages << endl;

        // Check root page explicitly
        void* root_page = pager_get_page(table->pager, table->pager->root_page_num);
        cout << "\nRoot Page (" << table->pager->root_page_num << "):" << endl;
        cout << "  Node type: " << (get_node_type(root_page) == NODE_LEAF ? "LEAF" : "INTERNAL") << endl;
        cout << "  Is root: " << (is_node_root(root_page) ? "YES" : "NO") << endl;
         if (get_node_type(root_page) == NODE_LEAF) {
            cout << "  Num cells: " << *get_leaf_node_num_cells(root_page) << endl;
            cout << "  Next leaf: " << *get_leaf_node_next_leaf(root_page) << endl;
        } else {
            cout << "  Num keys: " << *get_internal_node_num_keys(root_page) << endl;
            cout << "  Left child: " << *get_internal_node_child(root_page, 0) << endl;
            cout << "  Right child: " << *get_internal_node_right_child(root_page) << endl;
            if (*get_internal_node_num_keys(root_page) > 0)
                 cout << "  Key[0]: " << *get_internal_node_key(root_page, 0) << endl;
        }


        // Check other relevant pages if needed (e.g., children of root)
        // ...

        return META_COMMAND_SUCCESS;
    }
    else {
        return META_COMMAND_UNRECOGNIZED_COMMAND;
    }
}


// --- Parser Function  ---
// (No changes needed here)
PrepareResult prepare_statement(InputBuffer* input_buffer, Statement* statement) {
    // INSERT
    if (input_buffer->buffer.rfind("insert", 0) == 0) {
        statement->type = STATEMENT_INSERT;
        stringstream ss(input_buffer->buffer);
        string command;
        int32_t temp_id;
        ss >> command >> temp_id >> statement->row_to_insert.username >> statement->row_to_insert.email;

        if (ss.fail()) {
            return PREPARE_SYNTAX_ERROR;
        }

        // Validate ID is non-negative
        if (temp_id < 0) {
            cout << "Error: ID must be a non-negative integer." << endl;
            return PREPARE_SYNTAX_ERROR;
        }
        
        statement->row_to_insert.id = static_cast<uint32_t>(temp_id);

        if (strlen(statement->row_to_insert.username) > COLUMN_USERNAME_SIZE ||
            strlen(statement->row_to_insert.email) > COLUMN_EMAIL_SIZE) {
            return PREPARE_STRING_TOO_LONG;
        }
        return PREPARE_SUCCESS;
    }

    // SELECT
    if (input_buffer->buffer == "select") {
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }

    // FIND
    if (input_buffer->buffer.rfind("find", 0) == 0) {
        statement->type = STATEMENT_FIND;
        stringstream ss(input_buffer->buffer);
        string command;
        int32_t temp_id;
        ss >> command >> temp_id;
        if (ss.fail()) {
            return PREPARE_SYNTAX_ERROR;
        }
        
        // Validate ID is non-negative
        if (temp_id < 0) {
            cout << "Error: ID must be a non-negative integer." << endl;
            return PREPARE_SYNTAX_ERROR;
        }
        
        statement->row_to_insert.id = static_cast<uint32_t>(temp_id);
        return PREPARE_SUCCESS;
    }

    // DELETE - NEW
    if (input_buffer->buffer.rfind("delete", 0) == 0) {
        statement->type = STATEMENT_DELETE;
        stringstream ss(input_buffer->buffer);
        string command;
        int32_t temp_id;
        ss >> command >> temp_id;
        if (ss.fail()) {
            return PREPARE_SYNTAX_ERROR;
        }
        
        // Validate ID is non-negative
        if (temp_id < 0) {
            cout << "Error: ID must be a non-negative integer." << endl;
            return PREPARE_SYNTAX_ERROR;
        }
        
        statement->row_to_insert.id = static_cast<uint32_t>(temp_id);
        return PREPARE_SUCCESS;
    }

    // UPDATE - NEW
    if (input_buffer->buffer.rfind("update", 0) == 0) {
        statement->type = STATEMENT_UPDATE;
        stringstream ss(input_buffer->buffer);
        string command;
        int32_t temp_id;
        ss >> command >> temp_id >> statement->row_to_insert.username >> statement->row_to_insert.email;

        if (ss.fail()) {
            return PREPARE_SYNTAX_ERROR;
        }

        // Validate ID is non-negative
        if (temp_id < 0) {
            cout << "Error: ID must be a non-negative integer." << endl;
            return PREPARE_SYNTAX_ERROR;
        }
        
        statement->row_to_insert.id = static_cast<uint32_t>(temp_id);

        if (strlen(statement->row_to_insert.username) > COLUMN_USERNAME_SIZE ||
            strlen(statement->row_to_insert.email) > COLUMN_EMAIL_SIZE) {
            return PREPARE_STRING_TOO_LONG;
        }
        return PREPARE_SUCCESS;
    }

    // RANGE - NEW
    if (input_buffer->buffer.rfind("range", 0) == 0) {
        statement->type = STATEMENT_RANGE;
        stringstream ss(input_buffer->buffer);
        string command;
        int32_t temp_start, temp_end;
        ss >> command >> temp_start >> temp_end;
        if (ss.fail()) {
            return PREPARE_SYNTAX_ERROR;
        }
        
        // Validate both are non-negative
        if (temp_start < 0 || temp_end < 0) {
            cout << "Error: Range values must be non-negative integers." << endl;
            return PREPARE_SYNTAX_ERROR;
        }
        
        statement->range_start = static_cast<uint32_t>(temp_start);
        statement->range_end = static_cast<uint32_t>(temp_end);
        return PREPARE_SUCCESS;
    }

    return PREPARE_UNRECOGNIZED_STATEMENT;
}


// --- Storage/Serialization Functions  ---
// (No changes needed here)
void serialize_row(Row* source, void* destination) {
    memcpy(static_cast<char*>(destination) + ID_OFFSET, &(source->id), ID_SIZE);
    strncpy(static_cast<char*>(destination) + USERNAME_OFFSET, source->username, USERNAME_SIZE);
    strncpy(static_cast<char*>(destination) + EMAIL_OFFSET, source->email, EMAIL_SIZE);
}
void deserialize_row(void* source, Row* destination) {
    memcpy(&(destination->id), static_cast<char*>(source) + ID_OFFSET, ID_SIZE);
    strncpy(destination->username, static_cast<char*>(source) + USERNAME_OFFSET, USERNAME_SIZE);
    destination->username[USERNAME_SIZE] = '\0';
    strncpy(destination->email, static_cast<char*>(source) + EMAIL_OFFSET, EMAIL_SIZE);
    destination->email[EMAIL_SIZE] = '\0';
}


// --- B-TREE NODE ACCESSOR FUNCTIONS ---
// (No changes needed here, uses offsets correctly)

// Leaf Node
uint32_t* get_leaf_node_num_cells(void* node) {
    return reinterpret_cast<uint32_t*>(static_cast<char*>(node) + LEAF_NODE_NUM_CELLS_OFFSET);
}
void* get_leaf_node_cell(void* node, uint32_t cell_num) {
    return static_cast<char*>(node) + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
}
uint32_t* get_leaf_node_key(void* node, uint32_t cell_num) {
    return reinterpret_cast<uint32_t*>(get_leaf_node_cell(node, cell_num));
}
void* get_leaf_node_value(void* node, uint32_t cell_num) {
    return static_cast<char*>(get_leaf_node_cell(node, cell_num)) + LEAF_NODE_KEY_SIZE;
}
uint32_t* get_leaf_node_next_leaf(void* node) {
    return reinterpret_cast<uint32_t*>(static_cast<char*>(node) + LEAF_NODE_NEXT_LEAF_OFFSET);
}
void initialize_leaf_node(void* node) {
    set_node_type(node, NODE_LEAF);
    set_node_root(node, false);
    *get_leaf_node_num_cells(node) = 0;
    *get_leaf_node_next_leaf(node) = 0;
}

// Internal Node
void initialize_internal_node(void* node) {
    set_node_type(node, NODE_INTERNAL);
    set_node_root(node, false);
    *get_internal_node_num_keys(node) = 0;
}
uint32_t* get_internal_node_num_keys(void* node) {
    return reinterpret_cast<uint32_t*>(static_cast<char*>(node) + INTERNAL_NODE_NUM_KEYS_OFFSET);
}
uint32_t* get_internal_node_right_child(void* node) {
    return reinterpret_cast<uint32_t*>(static_cast<char*>(node) + INTERNAL_NODE_RIGHT_CHILD_OFFSET);
}
uint32_t* get_internal_node_child(void* node, uint32_t child_num) {
    uint32_t num_keys = *get_internal_node_num_keys(node);
    if (child_num > num_keys) {
        cout << "Error: Tried to access child_num " << child_num << " > num_keys " << num_keys << endl;
        exit(EXIT_FAILURE);
    }
    return reinterpret_cast<uint32_t*>(static_cast<char*>(node) + INTERNAL_NODE_HEADER_SIZE + child_num * INTERNAL_NODE_CELL_SIZE);
}
uint32_t* get_internal_node_key(void* node, uint32_t key_num) {
    void* child_ptr = get_internal_node_child(node, key_num);
    return reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(child_ptr) + INTERNAL_NODE_CHILD_SIZE);
}
uint32_t get_node_max_key(Pager* pager, void* node) {
    if (get_node_type(node) == NODE_LEAF) {
        // Handle empty leaf node case
        uint32_t num_cells = *get_leaf_node_num_cells(node);
        if (num_cells == 0) return 0; // Or handle appropriately
        return *get_leaf_node_key(node, num_cells - 1);
    } else {
        void* right_child = pager_get_page(
            pager,
            *get_internal_node_right_child(node)
        );
        return get_node_max_key(pager, right_child);
    }
}

// Common Node
NodeType get_node_type(void* node) {
    uint8_t value = *reinterpret_cast<uint8_t*>(static_cast<char*>(node) + NODE_TYPE_OFFSET);
    return (NodeType)value;
}
void set_node_type(void* node, NodeType type) {
    uint8_t value = type;
    *reinterpret_cast<uint8_t*>(static_cast<char*>(node) + NODE_TYPE_OFFSET) = value;
}
bool is_node_root(void* node) {
    uint8_t value = *reinterpret_cast<uint8_t*>(static_cast<char*>(node) + IS_ROOT_OFFSET);
    return (bool)value;
}
void set_node_root(void* node, bool is_root) {
    uint8_t value = is_root;
    *reinterpret_cast<uint8_t*>(static_cast<char*>(node) + IS_ROOT_OFFSET) = value;
}
uint32_t* get_node_parent(void* node) {
    return reinterpret_cast<uint32_t*>(static_cast<char*>(node) + PARENT_POINTER_OFFSET);
}

// --- B-TREE CURSOR/INSERT FUNCTIONS ---
// (No changes needed here, logic uses pager->root_page_num via table_find/start)

Cursor* leaf_node_find(Table* table, uint32_t page_num, uint32_t key) {
    void* node = pager_get_page(table->pager, page_num);
    uint32_t num_cells = *get_leaf_node_num_cells(node);

    Cursor* cursor = new Cursor();
    cursor->table = table;
    cursor->page_num = page_num;

    uint32_t min_index = 0;
    uint32_t max_index = num_cells;
    while (min_index != max_index) {
        uint32_t index = (min_index + max_index) / 2;
        uint32_t key_at_index = *get_leaf_node_key(node, index);
        if (key == key_at_index) {
            cursor->cell_num = index;
            cursor->end_of_table = false;
            return cursor;
        }
        if (key < key_at_index) {
            max_index = index;
        } else {
            min_index = index + 1;
        }
    }

    cursor->cell_num = min_index;
    cursor->end_of_table = (cursor->cell_num == num_cells);
    return cursor;
}

Cursor* table_find(Table* table, uint32_t key) {
    uint32_t page_num = table->pager->root_page_num; // Use pager's root
    void* node = pager_get_page(table->pager, page_num);

    while (get_node_type(node) == NODE_INTERNAL) {
        uint32_t num_keys = *get_internal_node_num_keys(node);
        uint32_t min_index = 0;
        uint32_t max_index = num_keys;

        while (min_index != max_index) {
            uint32_t index = (min_index + max_index) / 2;
            uint32_t key_to_compare = *get_internal_node_key(node, index);
            if (key <= key_to_compare) {
                max_index = index;
            } else {
                min_index = index + 1;
            }
        }

        if (min_index == num_keys) {
            page_num = *get_internal_node_right_child(node);
        } else {
            page_num = *get_internal_node_child(node, min_index);
        }

        node = pager_get_page(table->pager, page_num);
    }

    return leaf_node_find(table, page_num, key);
}

Cursor* table_start(Table* table) {
    Cursor* cursor = table_find(table, 0); // Find starting from the correct root
    void* node = pager_get_page(table->pager, cursor->page_num);
    cursor->end_of_table = (*get_leaf_node_num_cells(node) == 0);
    return cursor;
}

void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value) {
    void* node = pager_get_page(cursor->table->pager, cursor->page_num);
    uint32_t num_cells = *get_leaf_node_num_cells(node);

    if (cursor->cell_num < num_cells) {
        void* destination = get_leaf_node_cell(node, cursor->cell_num + 1);
        void* source = get_leaf_node_cell(node, cursor->cell_num);
        uint32_t bytes_to_move = (num_cells - cursor->cell_num) * LEAF_NODE_CELL_SIZE;
        memmove(destination, source, bytes_to_move);
    }

    *(get_leaf_node_num_cells(node)) += 1;
    *(get_leaf_node_key(node, cursor->cell_num)) = key;
    serialize_row(value, get_leaf_node_value(node, cursor->cell_num));
}

void create_new_root(Table* table, uint32_t right_child_page_num) {
    void* root = pager_get_page(table->pager, table->pager->root_page_num); // Get current root
    uint32_t left_child_page_num = table->pager->root_page_num; // Old root becomes left child
    void* right_child = pager_get_page(table->pager, right_child_page_num);

    uint32_t new_root_page_num = get_unused_page_num(table->pager);
    void* new_root = pager_get_page(table->pager, new_root_page_num);

    initialize_internal_node(new_root);
    set_node_root(new_root, true);
    *get_internal_node_num_keys(new_root) = 1;
    *get_internal_node_child(new_root, 0) = left_child_page_num;
    *get_internal_node_key(new_root, 0) = get_node_max_key(table->pager, root);
    *get_internal_node_right_child(new_root) = right_child_page_num;

    *get_node_parent(root) = new_root_page_num;
    *get_node_parent(right_child) = new_root_page_num;

    // Update the pager's root page number
    table->pager->root_page_num = new_root_page_num;

    set_node_root(root, false);
    set_node_root(right_child, false);
}

void internal_node_split_and_insert(Table* table, uint32_t parent_page_num, uint32_t child_page_num);

void internal_node_insert(Table* table, uint32_t parent_page_num, uint32_t child_page_num) {
    // Add a new child/key pair to parent that corresponds to child
    void* parent = pager_get_page(table->pager, parent_page_num);
    void* child = pager_get_page(table->pager, child_page_num);
    uint32_t child_max_key = get_node_max_key(table->pager, child);
    uint32_t index = *get_internal_node_num_keys(parent);

    // Find where to insert the new key
    for (uint32_t i = 0; i < index; i++) {
        if (child_max_key < *get_internal_node_key(parent, i)) {
            index = i;
            break;
        }
    }

    uint32_t original_num_keys = *get_internal_node_num_keys(parent);
    
    // Check if the node needs to be split
    if (original_num_keys >= INTERNAL_NODE_MAX_KEYS) {
        internal_node_split_and_insert(table, parent_page_num, child_page_num);
        return;
    }

    uint32_t right_child_page_num = *get_internal_node_right_child(parent);
    void* right_child = pager_get_page(table->pager, right_child_page_num);

    // If the new key is greater than all existing keys, update the right child
    if (child_max_key > get_node_max_key(table->pager, right_child)) {
        *get_internal_node_child(parent, original_num_keys) = right_child_page_num;
        *get_internal_node_key(parent, original_num_keys) = get_node_max_key(table->pager, right_child);
        *get_internal_node_right_child(parent) = child_page_num;
    } else {
        // Make room for the new cell by shifting cells to the right
        for (uint32_t i = original_num_keys; i > index; i--) {
            void* destination = get_internal_node_child(parent, i);
            void* source = get_internal_node_child(parent, i - 1);
            memcpy(destination, source, INTERNAL_NODE_CELL_SIZE);
        }
        *get_internal_node_child(parent, index) = child_page_num;
        *get_internal_node_key(parent, index) = child_max_key;
    }
    *get_internal_node_num_keys(parent) += 1;
}

void internal_node_split_and_insert(Table* table, uint32_t parent_page_num, uint32_t child_page_num) {
    void* old_node = pager_get_page(table->pager, parent_page_num);
    uint32_t old_num_keys = *get_internal_node_num_keys(old_node);
    
    // Create a new internal node
    uint32_t new_page_num = get_unused_page_num(table->pager);
    void* new_node = pager_get_page(table->pager, new_page_num);
    initialize_internal_node(new_node);
    
    // Allocate a temporary buffer to hold all keys and children including the new one
    uint32_t* temp_keys = new uint32_t[INTERNAL_NODE_MAX_KEYS + 1];
    uint32_t* temp_children = new uint32_t[INTERNAL_NODE_MAX_KEYS + 2];
    
    // Copy existing keys and children to temp arrays
    for (uint32_t i = 0; i < old_num_keys; i++) {
        temp_children[i] = *get_internal_node_child(old_node, i);
        temp_keys[i] = *get_internal_node_key(old_node, i);
    }
    temp_children[old_num_keys] = *get_internal_node_right_child(old_node);
    
    // Find where to insert the new child
    void* child = pager_get_page(table->pager, child_page_num);
    uint32_t child_max_key = get_node_max_key(table->pager, child);
    uint32_t insert_index = old_num_keys;
    
    for (uint32_t i = 0; i < old_num_keys; i++) {
        if (child_max_key < temp_keys[i]) {
            insert_index = i;
            break;
        }
    }
    
    // Insert the new key and child into the temp arrays
    for (uint32_t i = old_num_keys; i > insert_index; i--) {
        temp_keys[i] = temp_keys[i - 1];
        temp_children[i + 1] = temp_children[i];
    }
    temp_keys[insert_index] = child_max_key;
    temp_children[insert_index + 1] = child_page_num;
    
    // Split point: distribute keys evenly
    uint32_t split_at = (old_num_keys + 1) / 2;
    
    // Update old node with left half
    *get_internal_node_num_keys(old_node) = split_at;
    for (uint32_t i = 0; i < split_at; i++) {
        *get_internal_node_child(old_node, i) = temp_children[i];
        *get_internal_node_key(old_node, i) = temp_keys[i];
    }
    *get_internal_node_right_child(old_node) = temp_children[split_at];
    
    // Populate new node with right half
    uint32_t new_num_keys = old_num_keys - split_at;
    *get_internal_node_num_keys(new_node) = new_num_keys;
    for (uint32_t i = 0; i < new_num_keys; i++) {
        *get_internal_node_child(new_node, i) = temp_children[split_at + 1 + i];
        *get_internal_node_key(new_node, i) = temp_keys[split_at + 1 + i];
    }
    *get_internal_node_right_child(new_node) = temp_children[old_num_keys + 1];
    
    // Update parent pointers for all children of both nodes
    for (uint32_t i = 0; i <= split_at; i++) {
        void* child_node = pager_get_page(table->pager, *get_internal_node_child(old_node, i <= split_at - 1 ? i : split_at));
        if (i == split_at) {
            child_node = pager_get_page(table->pager, *get_internal_node_right_child(old_node));
        }
        *get_node_parent(child_node) = parent_page_num;
    }
    
    for (uint32_t i = 0; i <= new_num_keys; i++) {
        void* child_node = pager_get_page(table->pager, i < new_num_keys ? *get_internal_node_child(new_node, i) : *get_internal_node_right_child(new_node));
        *get_node_parent(child_node) = new_page_num;
    }
    
    // Update parent node
    *get_node_parent(new_node) = *get_node_parent(old_node);
    
    // Free temporary arrays
    delete[] temp_keys;
    delete[] temp_children;
    
    // If the old node is root, create a new root
    if (is_node_root(old_node)) {
        create_new_root(table, new_page_num);
    } else {
        // Insert the new node into its parent
        uint32_t parent_page_num_for_insert = *get_node_parent(old_node);
        uint32_t new_max = get_node_max_key(table->pager, old_node);
        void* parent = pager_get_page(table->pager, parent_page_num_for_insert);
        
        // Update the old max key in the parent
        uint32_t num_keys = *get_internal_node_num_keys(parent);
        for (uint32_t i = 0; i < num_keys; i++) {
            if (*get_internal_node_child(parent, i) == parent_page_num) {
                *get_internal_node_key(parent, i) = new_max;
                break;
            }
        }
        
        // Insert the new child into the parent (this may recursively split)
        internal_node_insert(table, parent_page_num_for_insert, new_page_num);
    }
}

void leaf_node_split_and_insert(Cursor* cursor, uint32_t key, Row* value) {
    void* old_node = pager_get_page(cursor->table->pager, cursor->page_num);

    uint32_t new_page_num = get_unused_page_num(cursor->table->pager);
    void* new_node = pager_get_page(cursor->table->pager, new_page_num);
    initialize_leaf_node(new_node);

    uint32_t old_next = *get_leaf_node_next_leaf(old_node);
    *get_leaf_node_next_leaf(new_node) = old_next;
    *get_leaf_node_next_leaf(old_node) = new_page_num;

    uint32_t num_cells = *get_leaf_node_num_cells(old_node);
    uint32_t split_at = (num_cells + 1) / 2;

    for (int32_t i = num_cells - 1; i >= split_at; i--) {
        void* destination_cell = get_leaf_node_cell(new_node, i - split_at);
        void* source_cell = get_leaf_node_cell(old_node, i);
        memcpy(destination_cell, source_cell, LEAF_NODE_CELL_SIZE);
    }

    *get_leaf_node_num_cells(old_node) = split_at;
    *get_leaf_node_num_cells(new_node) = num_cells - split_at;

    // Update parent pointers
    *get_node_parent(new_node) = *get_node_parent(old_node);

    if (is_node_root(old_node)) {
        create_new_root(cursor->table, new_page_num);
    } else {
        uint32_t parent_page_num = *get_node_parent(old_node);
        uint32_t new_max = get_node_max_key(cursor->table->pager, old_node);
        void* parent = pager_get_page(cursor->table->pager, parent_page_num);
        
        // Update the old max key in the parent
        uint32_t num_keys = *get_internal_node_num_keys(parent);
        for (uint32_t i = 0; i < num_keys; i++) {
            if (*get_internal_node_child(parent, i) == cursor->page_num) {
                *get_internal_node_key(parent, i) = new_max;
                break;
            }
        }
        
        // Insert the new child into the parent
        internal_node_insert(cursor->table, parent_page_num, new_page_num);
    }

    if (key <= *get_leaf_node_key(old_node, split_at - 1)) {
        Cursor* left_cursor = leaf_node_find(cursor->table, cursor->page_num, key);
        leaf_node_insert(left_cursor, key, value);
        delete left_cursor;
    } else {
        Cursor* right_cursor = leaf_node_find(cursor->table, new_page_num, key);
        leaf_node_insert(right_cursor, key, value);
        delete right_cursor;
    }
}

void leaf_node_delete(Cursor* cursor) {
    void* node = pager_get_page(cursor->table->pager, cursor->page_num);
    uint32_t num_cells = *get_leaf_node_num_cells(node);

    for (uint32_t i = cursor->cell_num; i < num_cells - 1; i++) {
        void* dest = get_leaf_node_cell(node, i);
        void* src = get_leaf_node_cell(node, i + 1);
        memcpy(dest, src, LEAF_NODE_CELL_SIZE);
    }

    *get_leaf_node_num_cells(node) -= 1;
}

// --- Helper function to print a row  ---
void print_row(Row* row) {
    cout << "(" << row->id << ", "
         << row->username << ", "
         << row->email << ")" << endl;
}

// --- Executor Functions ---
// (No changes needed, uses table_find which uses pager->root_page_num)
ExecuteResult execute_insert(Statement* statement, Table* table) {
    Row* row_to_insert = &(statement->row_to_insert);
    uint32_t key = row_to_insert->id;

    Cursor* cursor = table_find(table, key);

    void* node = pager_get_page(table->pager, cursor->page_num);
    uint32_t num_cells = *get_leaf_node_num_cells(node);

    if (cursor->cell_num < num_cells) {
        uint32_t key_at_index = *get_leaf_node_key(node, cursor->cell_num);
        if (key_at_index == key) {
            delete cursor;
            return EXECUTE_DUPLICATE_KEY;
        }
    }

    if (num_cells >= LEAF_NODE_MAX_CELLS) {
        leaf_node_split_and_insert(cursor, key, row_to_insert);
        delete cursor;
        return EXECUTE_SUCCESS;
    }

    leaf_node_insert(cursor, key, row_to_insert);

    delete cursor;
    return EXECUTE_SUCCESS;
}

ExecuteResult execute_select(Statement* statement, Table* table) {
    Cursor* cursor = table_start(table);
    Row row;
    uint32_t count = 0;

    while(!cursor->end_of_table) {
        void* leaf_node = pager_get_page(table->pager, cursor->page_num);
        uint32_t num_cells = *get_leaf_node_num_cells(leaf_node);

        while(cursor->cell_num < num_cells) {
            void* value = get_leaf_node_value(leaf_node, cursor->cell_num);
            deserialize_row(value, &row);
            print_row(&row);
            cursor->cell_num++;
            count++;
        }

        uint32_t next_leaf = *get_leaf_node_next_leaf(leaf_node);
        if (next_leaf == 0) {
            cursor->end_of_table = true;
        } else {
            cursor->page_num = next_leaf;
            cursor->cell_num = 0;
        }
    }

    cout << "Total rows: " << count << endl;
    delete cursor;
    return EXECUTE_SUCCESS;
}

ExecuteResult execute_find(Statement* statement, Table* table) {
    uint32_t key_to_find = statement->row_to_insert.id;
    Cursor* cursor = table_find(table, key_to_find);

    void* node = pager_get_page(table->pager, cursor->page_num);
    uint32_t num_cells = *get_leaf_node_num_cells(node);

    if (cursor->cell_num < num_cells) {
        uint32_t key_at_index = *get_leaf_node_key(node, cursor->cell_num);
        if (key_at_index == key_to_find) {
            Row row;
            void* value = get_leaf_node_value(node, cursor->cell_num);
            deserialize_row(value, &row);
            print_row(&row);

            delete cursor;
            return EXECUTE_SUCCESS;
        }
    }

    delete cursor;
    return EXECUTE_RECORD_NOT_FOUND;
}

ExecuteResult execute_delete(Statement* statement, Table* table) {
    uint32_t key_to_delete = statement->row_to_insert.id;
    Cursor* cursor = table_find(table, key_to_delete);

    void* node = pager_get_page(table->pager, cursor->page_num);
    uint32_t num_cells = *get_leaf_node_num_cells(node);

    if (cursor->cell_num < num_cells) {
        uint32_t key_at_index = *get_leaf_node_key(node, cursor->cell_num);
        if (key_at_index == key_to_delete) {
            leaf_node_delete(cursor);
            delete cursor;
            return EXECUTE_SUCCESS;
        }
    }

    delete cursor;
    return EXECUTE_RECORD_NOT_FOUND;
}

ExecuteResult execute_update(Statement* statement, Table* table) {
    uint32_t key_to_update = statement->row_to_insert.id;
    Cursor* cursor = table_find(table, key_to_update);

    void* node = pager_get_page(table->pager, cursor->page_num);
    uint32_t num_cells = *get_leaf_node_num_cells(node);

    if (cursor->cell_num < num_cells) {
        uint32_t key_at_index = *get_leaf_node_key(node, cursor->cell_num);
        if (key_at_index == key_to_update) {
            Row* row_to_update = &(statement->row_to_insert);
            serialize_row(row_to_update, get_leaf_node_value(node, cursor->cell_num));
            delete cursor;
            return EXECUTE_SUCCESS;
        }
    }

    delete cursor;
    return EXECUTE_RECORD_NOT_FOUND;
}

ExecuteResult execute_range(Statement* statement, Table* table) {
    uint32_t start_key = statement->range_start;
    uint32_t end_key = statement->range_end;

    if (start_key > end_key) {
        cout << "Error: Invalid range (start > end)" << endl;
        return EXECUTE_SUCCESS;
    }

    Cursor* cursor = table_find(table, start_key);
    Row row;
    uint32_t count = 0;

    while(!cursor->end_of_table) {
        void* leaf_node = pager_get_page(table->pager, cursor->page_num);
        uint32_t num_cells = *get_leaf_node_num_cells(leaf_node);

        while(cursor->cell_num < num_cells) {
            uint32_t current_key = *get_leaf_node_key(leaf_node, cursor->cell_num);

            if (current_key > end_key) {
                cout << "Total rows in range: " << count << endl;
                delete cursor;
                return EXECUTE_SUCCESS;
            }

            if (current_key >= start_key && current_key <= end_key) {
                void* value = get_leaf_node_value(leaf_node, cursor->cell_num);
                deserialize_row(value, &row);
                print_row(&row);
                count++;
            }

            cursor->cell_num++;
        }

        uint32_t next_leaf = *get_leaf_node_next_leaf(leaf_node);
        if (next_leaf == 0) {
            cursor->end_of_table = true;
        } else {
            cursor->page_num = next_leaf;
            cursor->cell_num = 0;
        }
    }

    cout << "Total rows in range: " << count << endl;
    delete cursor;
    return EXECUTE_SUCCESS;
}

ExecuteResult execute_statement(Statement* statement, Table* table) {
    switch (statement->type) {
        case (STATEMENT_INSERT):
            return execute_insert(statement, table);
        case (STATEMENT_SELECT):
            return execute_select(statement, table);
        case (STATEMENT_FIND):
            return execute_find(statement, table);
        case (STATEMENT_DELETE):
            return execute_delete(statement, table);
        case (STATEMENT_UPDATE):
            return execute_update(statement, table);
        case (STATEMENT_RANGE):
            return execute_range(statement, table);
    }
    return EXECUTE_SUCCESS;
}

// NEW: Tree visualization functions
void print_node(Pager* pager, uint32_t page_num, uint32_t indent_level) {
    void* node = pager_get_page(pager, page_num);
    uint32_t num_keys;

    for (uint32_t i = 0; i < indent_level; i++) {
        cout << "  ";
    }

    if (get_node_type(node) == NODE_LEAF) {
        num_keys = *get_leaf_node_num_cells(node);
        cout << "- leaf (page " << page_num << ", size " << num_keys << ", next " << *get_leaf_node_next_leaf(node) << ")" << endl;

        for (uint32_t i = 0; i < num_keys; i++) {
            for (uint32_t j = 0; j < indent_level + 1; j++) {
                cout << "  ";
            }
            cout << "- " << *get_leaf_node_key(node, i) << endl;
        }
    } else {
        num_keys = *get_internal_node_num_keys(node);
        cout << "- internal (page " << page_num << ", size " << num_keys << ")" << endl;

        for (uint32_t i = 0; i < num_keys; i++) {
            uint32_t child = *get_internal_node_child(node, i);
            print_node(pager, child, indent_level + 1);

            for (uint32_t j = 0; j < indent_level + 1; j++) {
                cout << "  ";
            }
            cout << "- key " << *get_internal_node_key(node, i) << endl;
        }

        uint32_t right_child = *get_internal_node_right_child(node);
        print_node(pager, right_child, indent_level + 1);
    }
}

void print_tree(Table* table) {
    print_node(table->pager, table->pager->root_page_num, 0); // Use pager's root
}

// --- REPL (Main)  ---
void print_prompt() { 
    cout << "db > ";
    cout.flush(); // Ensure prompt is displayed immediately
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cout << "Must supply a database filename." << endl;
        exit(EXIT_FAILURE);
    }

    string filename = argv[1];
    Table* table = new_table(filename);
    InputBuffer* input_buffer = new_input_buffer();

    cout << "Enhanced SQLite Clone - Type '.exit' to quit, '.btree' to visualize tree" << endl;

    while (true) {
        print_prompt();
        read_input(input_buffer);

        if (input_buffer->buffer.empty()) {
            continue;
        }

        if (input_buffer->buffer[0] == '.') {
            switch (do_meta_command(input_buffer, table)) {
                case (META_COMMAND_SUCCESS):
                    continue;
                case (META_COMMAND_UNRECOGNIZED_COMMAND):
                    cout << "Unrecognized command '" << input_buffer->buffer << "'" << endl;
                    continue;
            }
        }

        Statement statement;
        switch (prepare_statement(input_buffer, &statement)) {
            case (PREPARE_SUCCESS):
                break;
            case (PREPARE_SYNTAX_ERROR):
                cout << "Syntax error. Could not parse statement." << endl;
                continue;
            case (PREPARE_STRING_TOO_LONG):
                cout << "Error: String is too long." << endl;
                continue;
            case (PREPARE_UNRECOGNIZED_STATEMENT):
                cout << "Unrecognized keyword at start of '" << input_buffer->buffer << "'." << endl;
                continue;
        }

        switch (execute_statement(&statement, table)) {
            case (EXECUTE_SUCCESS):
                cout << "Executed." << endl;
                break;
            case (EXECUTE_TABLE_FULL):
                cout << "Error: Table full." << endl;
                break;
            case (EXECUTE_DUPLICATE_KEY):
                cout << "Error: Duplicate key." << endl;
                break;
            case (EXECUTE_RECORD_NOT_FOUND):
                cout << "Error: Record not found." << endl;
                break;
        }
    }
}