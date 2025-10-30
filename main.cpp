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
        pager->free_head = 0;     // No free pages initially
        pager->num_pages = 1;     // Start with one page (the root)
        pager->file_length = PAGE_SIZE + DB_FILE_HEADER_SIZE; // Header + 1 page

        // Write the initial header (root_page_num + free_head)
        pager->file_stream.seekp(0);
        pager->file_stream.write(reinterpret_cast<char*>(&pager->root_page_num), sizeof(pager->root_page_num));
        pager->file_stream.write(reinterpret_cast<char*>(&pager->free_head), sizeof(pager->free_head));
        // Ensure file is large enough for header + initial page
        char zero_page[PAGE_SIZE] = {0};
        pager->file_stream.seekp(DB_FILE_HEADER_SIZE);
        pager->file_stream.write(zero_page, PAGE_SIZE);
        pager->file_stream.flush(); // Make sure header is written
        
        // Note: The root page will be properly initialized in new_table()

    } else {
        // File exists, read header (root_page_num + free_head)
        pager->file_stream.seekg(0);
        pager->file_stream.read(reinterpret_cast<char*>(&pager->root_page_num), sizeof(pager->root_page_num));
        pager->file_stream.read(reinterpret_cast<char*>(&pager->free_head), sizeof(pager->free_head));
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


    // No need to initialize pages array; page_cache starts empty
    return pager;
}

// Calculates the byte offset for a given page number in the file
uint64_t get_page_file_offset(uint32_t page_num) {
    return DB_FILE_HEADER_SIZE + (uint64_t)page_num * PAGE_SIZE;
}


void* pager_get_page(Pager* pager, uint32_t page_num) {
    if (page_num >= TABLE_MAX_PAGES) {
        cout << "Error: Tried to access page number out of bounds: " << page_num << endl;
        return nullptr;
    }

    // LRU Cache logic
    if (pager->page_cache.count(page_num)) {
        // Cache hit: move to front (MRU)
        pager->lru_list.erase(pager->lru_map[page_num]);
        pager->lru_list.push_front(page_num);
        pager->lru_map[page_num] = pager->lru_list.begin();
        return pager->page_cache[page_num];
    }

    // Cache miss: evict if full
    if (pager->page_cache.size() >= PAGER_CACHE_SIZE) {
        uint32_t lru_page_num = pager->lru_list.back();
        void* lru_page_data = pager->page_cache[lru_page_num];
        pager_flush(pager, lru_page_num);
        pager->lru_list.pop_back();
        pager->lru_map.erase(lru_page_num);
        delete[] static_cast<char*>(lru_page_data);
        pager->page_cache.erase(lru_page_num);
    }

    // Load page from disk
    void* new_page = new char[PAGE_SIZE];
    memset(new_page, 0, PAGE_SIZE);
    uint64_t file_offset = get_page_file_offset(page_num);
    uint32_t current_num_pages = (pager->file_length - DB_FILE_HEADER_SIZE) / PAGE_SIZE;
    if (page_num < current_num_pages) {
        pager->file_stream.seekg(file_offset);
        pager->file_stream.read(static_cast<char*>(new_page), PAGE_SIZE);
        if (pager->file_stream.fail()) {
            cout << "Error: Failed to read page " << page_num << " from file." << endl;
            delete[] static_cast<char*>(new_page);
            return nullptr;
        }
        if (pager->file_stream.eof()) {
            pager->file_stream.clear();
        }
    }
    // Add to cache (MRU)
    pager->page_cache[page_num] = new_page;
    pager->lru_list.push_front(page_num);
    pager->lru_map[page_num] = pager->lru_list.begin();
    // Update num_pages if this is the highest page number seen so far
    if (page_num >= pager->num_pages) {
        pager->num_pages = page_num + 1;
    }
    return new_page;
}

PagerResult pager_flush(Pager* pager, uint32_t page_num) {
    // Use page_cache instead of pages array
    if (pager->page_cache.count(page_num) == 0) {
        return PAGER_NULL_PAGE;
    }
    void* page_data = pager->page_cache[page_num];
    pager->file_stream.seekp(get_page_file_offset(page_num));
    pager->file_stream.write(static_cast<char*>(page_data), PAGE_SIZE);
    if (pager->file_stream.fail()) {
        return PAGER_DISK_ERROR;
    }
    // Ensure file_length is updated if we wrote a new page
    uint64_t expected_length = get_page_file_offset(page_num) + PAGE_SIZE;
    if (expected_length > pager->file_length) {
        pager->file_length = expected_length;
    }
    return PAGER_SUCCESS;
}

// Validate freelist: Check for cycles and invalid page numbers
bool validate_free_chain(Pager* pager) {
    if (pager->free_head == 0) {
        return true; // Empty freelist is valid
    }
    
    // Use a set to detect cycles - if we see a page twice, there's a cycle
    std::set<uint32_t> visited;
    uint32_t current = pager->free_head;
    
    while (current != 0) {
        // Check for invalid page number
        if (current >= TABLE_MAX_PAGES) {
            cout << "ERROR: Freelist contains invalid page number " << current << endl;
            return false;
        }
        
        // Check for cycle
        if (visited.find(current) != visited.end()) {
            cout << "ERROR: Freelist contains cycle at page " << current << endl;
            return false;
        }
        
        visited.insert(current);
        
        // Get next page in chain
        void* page = pager_get_page(pager, current);
        if (page == nullptr) {
            cout << "ERROR: Cannot load freelist page " << current << endl;
            return false;
        }
        
        uint32_t next_free;
        memcpy(&next_free, page, sizeof(uint32_t));
        current = next_free;
        
        // Safety: Limit chain length to prevent infinite loops
        if (visited.size() > pager->num_pages) {
            cout << "ERROR: Freelist chain too long (possible corruption)" << endl;
            return false;
        }
    }
    
    return true;
}

uint32_t get_unused_page_num(Pager* pager) {
    // Persistent freelist: use free_head and linked list in file
    if (pager->free_head == 0) {
        // No free pages, allocate a new one
        return pager->num_pages++;
    }
    // Reuse page from freelist
    uint32_t page_num = pager->free_head;
    void* page = pager_get_page(pager, page_num);
    if (page == nullptr) {
        // Critical error: fallback to new page
        return pager->num_pages++;
    }
    // Read next free page from first 4 bytes
    memcpy(&pager->free_head, page, sizeof(uint32_t));
    // Clear page before reuse
    memset(page, 0, PAGE_SIZE);
    return page_num;
}

void free_page(Pager* pager, uint32_t page_num) {
    // Persistent freelist: add page to front of linked list
    if (pager == nullptr || page_num >= TABLE_MAX_PAGES) {
        return;
    }
    void* page = pager_get_page(pager, page_num);
    if (page == nullptr) {
        return;
    }
    // Write current free_head into first 4 bytes of freed page
    memcpy(page, &pager->free_head, sizeof(uint32_t));
    pager->free_head = page_num;
    // Flush this page immediately
    pager_flush(pager, page_num);
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

    // Flush all cached pages in LRU cache
    bool had_flush_error = false;
    for (auto const& [page_num, page_data] : pager->page_cache) {
        if (pager_flush(pager, page_num) != PAGER_SUCCESS) {
            cout << "Warning: Failed to flush page " << page_num << ". Data may be lost." << endl;
            had_flush_error = true;
        }
        delete[] static_cast<char*>(page_data);
    }
    pager->page_cache.clear();
    pager->lru_list.clear();
    pager->lru_map.clear();

    // --- WRITE FINAL HEADER ---
    if (pager->file_stream.is_open()) {
        pager->file_stream.seekp(0);
        pager->file_stream.write(reinterpret_cast<char*>(&pager->root_page_num), sizeof(uint32_t));
        pager->file_stream.write(reinterpret_cast<char*>(&pager->free_head), sizeof(uint32_t));
        pager->file_stream.flush();
        pager->file_stream.close();
    }

    if (had_flush_error) {
        cout << "Warning: Database close completed with errors." << endl;
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
    } else if (input_buffer->buffer == ".validate") {
        validate_tree(table);
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
        // Return nullptr instead of exit(EXIT_FAILURE)
        return nullptr;
    }
    return reinterpret_cast<uint32_t*>(static_cast<char*>(node) + INTERNAL_NODE_HEADER_SIZE + child_num * INTERNAL_NODE_CELL_SIZE);
}
uint32_t* get_internal_node_key(void* node, uint32_t key_num) {
    void* child_ptr = get_internal_node_child(node, key_num);
    return reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(child_ptr) + INTERNAL_NODE_CHILD_SIZE);
}
uint32_t get_node_max_key(Pager* pager, void* node) {
    if (node == nullptr) {
        cout << "Error: get_node_max_key called with nullptr" << endl;
        return 0;
    }
    
    if (get_node_type(node) == NODE_LEAF) {
        // Handle empty leaf node case
        uint32_t num_cells = *get_leaf_node_num_cells(node);
        if (num_cells == 0) {
            cout << "Warning: Trying to get max key from empty leaf node" << endl;
            return 0;
        }
        return *get_leaf_node_key(node, num_cells - 1);
    } else {
        uint32_t right_child_page = *get_internal_node_right_child(node);
        if (right_child_page >= TABLE_MAX_PAGES) {
            cout << "Error: Invalid right child page number: " << right_child_page << endl;
            return 0;
        }
        void* right_child = pager_get_page(pager, right_child_page);
        if (right_child == nullptr) {
            cout << "Error: Could not get right child page" << endl;
            return 0;
        }
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
    if (node == nullptr) {
        return nullptr;
    }
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
    if (node == nullptr) {
        return nullptr;
    }

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
            uint32_t* child_ptr = get_internal_node_child(node, min_index);
            if (child_ptr == nullptr) {
                return nullptr;
            }
            page_num = *child_ptr;
        }

        node = pager_get_page(table->pager, page_num);
        if (node == nullptr) {
            return nullptr;
        }
    }

    return leaf_node_find(table, page_num, key);
}

Cursor* table_start(Table* table) {
    Cursor* cursor = table_find(table, 0); // Find starting from the correct root
    if (cursor == nullptr) {
        return nullptr;
    }
    void* node = pager_get_page(table->pager, cursor->page_num);
    if (node == nullptr) {
        delete cursor;
        return nullptr;
    }
    
    // CRITICAL FIX: Handle empty root after merge
    // If root is empty (all cells deleted), mark end_of_table
    uint32_t num_cells = *get_leaf_node_num_cells(node);
    cursor->end_of_table = (num_cells == 0);
    
    return cursor;
}

void leaf_node_insert(Cursor* cursor, uint32_t key, Row* value) {
    void* node = pager_get_page(cursor->table->pager, cursor->page_num);
    if (node == nullptr) {
        return;
    }
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
    if (root == nullptr) {
        return;
    }
    uint32_t left_child_page_num = table->pager->root_page_num; // Old root becomes left child
    void* right_child = pager_get_page(table->pager, right_child_page_num);
    if (right_child == nullptr) {
        return;
    }

    uint32_t new_root_page_num = get_unused_page_num(table->pager);
    void* new_root = pager_get_page(table->pager, new_root_page_num);
    if (new_root == nullptr) {
        return;
    }

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
    if (parent == nullptr) {
        return;
    }
    void* child = pager_get_page(table->pager, child_page_num);
    if (child == nullptr) {
        return;
    }
    uint32_t child_max_key = get_node_max_key(table->pager, child);
    uint32_t original_num_keys = *get_internal_node_num_keys(parent);

    // Binary search to find insertion index
    uint32_t min_index = 0;
    uint32_t max_index = original_num_keys;
    while (min_index != max_index) {
        uint32_t mid = (min_index + max_index) / 2;
        uint32_t key_at_mid = *get_internal_node_key(parent, mid);
        if (child_max_key < key_at_mid) {
            max_index = mid;
        } else {
            min_index = mid + 1;
        }
    }
    uint32_t index = min_index;
    
    // Check if the node needs to be split
    if (original_num_keys >= INTERNAL_NODE_MAX_KEYS) {
        internal_node_split_and_insert(table, parent_page_num, child_page_num);
        return;
    }

    uint32_t right_child_page_num = *get_internal_node_right_child(parent);
    void* right_child = pager_get_page(table->pager, right_child_page_num);
    if (right_child == nullptr) {
        return;
    }

    // If the new key is greater than all existing keys, update the right child
    if (child_max_key > get_node_max_key(table->pager, right_child)) {
    uint32_t* child_ptr = get_internal_node_child(parent, original_num_keys);
    if (child_ptr) *child_ptr = right_child_page_num;
    *get_internal_node_key(parent, original_num_keys) = get_node_max_key(table->pager, right_child);
    *get_internal_node_right_child(parent) = child_page_num;
    } else {
        // Make room for the new cell by shifting cells to the right
        for (uint32_t i = original_num_keys; i > index; i--) {
            uint32_t* dest_ptr = get_internal_node_child(parent, i);
            uint32_t* src_ptr = get_internal_node_child(parent, i - 1);
            if (dest_ptr && src_ptr) memcpy(dest_ptr, src_ptr, INTERNAL_NODE_CELL_SIZE);
        }
        uint32_t* child_ptr = get_internal_node_child(parent, index);
        if (child_ptr) *child_ptr = child_page_num;
        *get_internal_node_key(parent, index) = child_max_key;
    }
    *get_internal_node_num_keys(parent) += 1;
}

void internal_node_split_and_insert(Table* table, uint32_t parent_page_num, uint32_t child_page_num) {
    void* old_node = pager_get_page(table->pager, parent_page_num);
    if (old_node == nullptr) {
        return;
    }
    uint32_t old_num_keys = *get_internal_node_num_keys(old_node);
    
    // Create a new internal node
    uint32_t new_page_num = get_unused_page_num(table->pager);
    void* new_node = pager_get_page(table->pager, new_page_num);
    if (new_node == nullptr) {
        return;
    }
    initialize_internal_node(new_node);
    
    // Allocate a temporary buffer to hold all keys and children including the new one
    // Zero-initialize to avoid garbage values
    uint32_t* temp_keys = new uint32_t[INTERNAL_NODE_MAX_KEYS + 1]();
    uint32_t* temp_children = new uint32_t[INTERNAL_NODE_MAX_KEYS + 2]();
    
    // Copy existing keys and children to temp arrays
    for (uint32_t i = 0; i < old_num_keys; i++) {
    uint32_t* child_ptr = get_internal_node_child(old_node, i);
    temp_children[i] = child_ptr ? *child_ptr : 0;
        temp_keys[i] = *get_internal_node_key(old_node, i);
    }
    temp_children[old_num_keys] = *get_internal_node_right_child(old_node);
    
    // Find where to insert the new child
    void* child = pager_get_page(table->pager, child_page_num);
    if (child == nullptr) {
        delete[] temp_keys;
        delete[] temp_children;
        return;
    }
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
    uint32_t* child_ptr = get_internal_node_child(old_node, i);
    if (child_ptr) *child_ptr = temp_children[i];
        *get_internal_node_key(old_node, i) = temp_keys[i];
    }
    *get_internal_node_right_child(old_node) = temp_children[split_at];
    
    // Populate new node with right half
    // CRITICAL FIX: new_num_keys should be (old_num_keys + 1) - split_at
    // because we inserted one new key into the temp array
    uint32_t new_num_keys = (old_num_keys + 1) - split_at;
    *get_internal_node_num_keys(new_node) = new_num_keys;
    for (uint32_t i = 0; i < new_num_keys; i++) {
    uint32_t* child_ptr = get_internal_node_child(new_node, i);
    if (child_ptr) *child_ptr = temp_children[split_at + 1 + i];
        *get_internal_node_key(new_node, i) = temp_keys[split_at + 1 + i];
    }
    *get_internal_node_right_child(new_node) = temp_children[old_num_keys + 1];
    
    // Update parent pointers for all children of both nodes
    for (uint32_t i = 0; i <= split_at; i++) {
    uint32_t* child_ptr = get_internal_node_child(old_node, i <= split_at - 1 ? i : split_at);
    void* child_node = child_ptr ? pager_get_page(table->pager, *child_ptr) : nullptr;
        if (i == split_at) {
            child_node = pager_get_page(table->pager, *get_internal_node_right_child(old_node));
        }
        *get_node_parent(child_node) = parent_page_num;
    }
    
    for (uint32_t i = 0; i <= new_num_keys; i++) {
    uint32_t* child_ptr = i < new_num_keys ? get_internal_node_child(new_node, i) : get_internal_node_right_child(new_node);
    void* child_node = child_ptr ? pager_get_page(table->pager, *child_ptr) : nullptr;
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
    if (old_node == nullptr) {
        return;
    }

    uint32_t new_page_num = get_unused_page_num(cursor->table->pager);
    void* new_node = pager_get_page(cursor->table->pager, new_page_num);
    if (new_node == nullptr) {
        return;
    }
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
    if (node == nullptr) {
        return;
    }
    uint32_t num_cells = *get_leaf_node_num_cells(node);

    for (uint32_t i = cursor->cell_num; i < num_cells - 1; i++) {
        void* dest = get_leaf_node_cell(node, i);
        void* src = get_leaf_node_cell(node, i + 1);
        memcpy(dest, src, LEAF_NODE_CELL_SIZE);
    }

    *get_leaf_node_num_cells(node) -= 1;
    
    // Check for underflow (only if not root)
    if (!is_node_root(node) && *get_leaf_node_num_cells(node) < LEAF_NODE_MIN_CELLS) {
        handle_leaf_underflow(cursor->table, cursor->page_num);
    }
}

// Helper: Find the index of a child in its parent
int32_t find_child_index_in_parent(void* parent, uint32_t child_page_num) {
    uint32_t num_keys = *get_internal_node_num_keys(parent);
    
    // Check right child first (common case for rightmost operations)
    if (*get_internal_node_right_child(parent) == child_page_num) {
        return num_keys; // Right child is at index num_keys
    }
    
    // OPTIMIZATION: Use binary search for O(log n) instead of O(n)
    // Note: Children may not be sorted by page number, so we use linear search
    // A true optimization would require maintaining a sorted index
    for (uint32_t i = 0; i < num_keys; i++) {
        if (*get_internal_node_child(parent, i) == child_page_num) {
            return i;
        }
    }
    
    return -1; // Not found
}

// Handle leaf node underflow by borrowing or merging
void handle_leaf_underflow(Table* table, uint32_t page_num) {
    // SAFETY: Comprehensive null guards
    if (table == nullptr || table->pager == nullptr) {
        cout << "Error: Null table or pager in handle_leaf_underflow" << endl;
        return;
    }
    
    void* node = pager_get_page(table->pager, page_num);
    if (node == nullptr) {
        cout << "Error: Could not load page " << page_num << " in handle_leaf_underflow" << endl;
        return;
    }
    
    uint32_t parent_page_num = *get_node_parent(node);
    
    // If this is root, no underflow handling needed (root can have fewer cells)
    if (is_node_root(node)) {
        return;
    }
    
    void* parent = pager_get_page(table->pager, parent_page_num);
    if (parent == nullptr) {
        cout << "Error: Could not load parent page " << parent_page_num << " in handle_leaf_underflow" << endl;
        return;
    }
    
    int32_t child_index = find_child_index_in_parent(parent, page_num);
    
    if (child_index < 0) {
        cout << "Error: Could not find child in parent during underflow handling" << endl;
        return;
    }
    
    uint32_t num_parent_keys = *get_internal_node_num_keys(parent);
    
    // Try to borrow from right sibling first
    if ((uint32_t)child_index < num_parent_keys) {
        uint32_t right_sibling_page_num;
        if ((uint32_t)child_index == num_parent_keys - 1) {
            right_sibling_page_num = *get_internal_node_right_child(parent);
        } else {
            right_sibling_page_num = *get_internal_node_child(parent, child_index + 1);
        }
        
        void* right_sibling = pager_get_page(table->pager, right_sibling_page_num);
        if (right_sibling == nullptr) {
        } else {
            uint32_t right_sibling_cells = *get_leaf_node_num_cells(right_sibling);
            
            // If right sibling has extra cells, borrow one
            if (right_sibling_cells > LEAF_NODE_MIN_CELLS) {
            // Move the first cell from right sibling to end of current node
            uint32_t current_cells = *get_leaf_node_num_cells(node);
            void* dest = get_leaf_node_cell(node, current_cells);
            void* src = get_leaf_node_cell(right_sibling, 0);
            memcpy(dest, src, LEAF_NODE_CELL_SIZE);
            
            // Shift cells in right sibling left
            // CRITICAL FIX: Use memmove instead of memcpy for overlapping memory regions
            for (uint32_t i = 0; i < right_sibling_cells - 1; i++) {
                void* dest_sib = get_leaf_node_cell(right_sibling, i);
                void* src_sib = get_leaf_node_cell(right_sibling, i + 1);
                memmove(dest_sib, src_sib, LEAF_NODE_CELL_SIZE);
            }
            
            *get_leaf_node_num_cells(node) = current_cells + 1;
            *get_leaf_node_num_cells(right_sibling) = right_sibling_cells - 1;
            
            // CRITICAL FIX: Update parent key to the NEW first key of right sibling (borrowed key)
            // not the new max of current node
            *get_internal_node_key(parent, child_index) = *get_leaf_node_key(right_sibling, 0);
            
            return; // Successfully borrowed
            }
        }
    }
    
    // Try to borrow from left sibling
    if (child_index > 0) {
        uint32_t left_sibling_page_num = *get_internal_node_child(parent, child_index - 1);
        void* left_sibling = pager_get_page(table->pager, left_sibling_page_num);
        if (left_sibling == nullptr) {
        } else {
            uint32_t left_sibling_cells = *get_leaf_node_num_cells(left_sibling);
            
            // If left sibling has extra cells, borrow one
            if (left_sibling_cells > LEAF_NODE_MIN_CELLS) {
            uint32_t current_cells = *get_leaf_node_num_cells(node);
            
            // Make room at the beginning of current node
            // CRITICAL FIX: Use memmove instead of memcpy for overlapping memory regions
            for (int32_t i = current_cells; i > 0; i--) {
                void* dest = get_leaf_node_cell(node, i);
                void* src = get_leaf_node_cell(node, i - 1);
                memmove(dest, src, LEAF_NODE_CELL_SIZE);
            }
            
            // Move last cell from left sibling to beginning of current node
            void* dest = get_leaf_node_cell(node, 0);
            void* src = get_leaf_node_cell(left_sibling, left_sibling_cells - 1);
            memcpy(dest, src, LEAF_NODE_CELL_SIZE);
            
            *get_leaf_node_num_cells(node) = current_cells + 1;
            *get_leaf_node_num_cells(left_sibling) = left_sibling_cells - 1;
            
            // Update parent key for left sibling
            uint32_t new_left_max = get_node_max_key(table->pager, left_sibling);
            *get_internal_node_key(parent, child_index - 1) = new_left_max;
            
            return; // Successfully borrowed
            }
        }
    }
    
    // Cannot borrow - must merge with a sibling
    // Prefer merging with left sibling if it exists
    if (child_index > 0) {
        // Merge with left sibling
        uint32_t left_sibling_page_num = *get_internal_node_child(parent, child_index - 1);
        void* left_sibling = pager_get_page(table->pager, left_sibling_page_num);
        uint32_t left_cells = *get_leaf_node_num_cells(left_sibling);
        uint32_t current_cells = *get_leaf_node_num_cells(node);
        
        // Copy all cells from current node to left sibling
        for (uint32_t i = 0; i < current_cells; i++) {
            void* dest = get_leaf_node_cell(left_sibling, left_cells + i);
            void* src = get_leaf_node_cell(node, i);
            memcpy(dest, src, LEAF_NODE_CELL_SIZE);
        }
        
        *get_leaf_node_num_cells(left_sibling) = left_cells + current_cells;
        
        // Update next leaf pointer
        *get_leaf_node_next_leaf(left_sibling) = *get_leaf_node_next_leaf(node);
        
        // CRITICAL FIX: Update left sibling's max key in parent BEFORE shifting
        // Must do this before removing entries while indices are still correct
        uint32_t new_left_max = get_node_max_key(table->pager, left_sibling);
        if (child_index > 0) {
            *get_internal_node_key(parent, child_index - 1) = new_left_max;
        }
        
        // Remove current node's entry from parent
        // Shift keys and children left
        // CRITICAL FIX: Use memmove for safe overlapping memory operations
        if (child_index < num_parent_keys - 1) {
            // Shift children
            memmove(get_internal_node_child(parent, child_index),
                   get_internal_node_child(parent, child_index + 1),
                   (num_parent_keys - child_index - 1) * sizeof(uint32_t));
            // Shift keys
            memmove(get_internal_node_key(parent, child_index),
                   get_internal_node_key(parent, child_index + 1),
                   (num_parent_keys - child_index - 1) * sizeof(uint32_t));
        }
        
        // If we removed the last key, update child pointer
        if ((uint32_t)child_index == num_parent_keys - 1) {
            // The child at the removed position becomes the right child's old value
            *get_internal_node_child(parent, num_parent_keys - 1) = *get_internal_node_right_child(parent);
        }
        
        // Update parent key count
        *get_internal_node_num_keys(parent) = num_parent_keys - 1;
        
        // CRITICAL FIX: After shifting, update all shifted keys to reflect their actual child max keys
        // This is necessary because we just removed a node, and the keys may no longer be accurate
        for (uint32_t i = child_index - 1; i < *get_internal_node_num_keys(parent); i++) {
            uint32_t child_page = *get_internal_node_child(parent, i);
            void* child_node = pager_get_page(table->pager, child_page);
            if (child_node != nullptr) {
                uint32_t child_max = get_node_max_key(table->pager, child_node);
                *get_internal_node_key(parent, i) = child_max;
            }
        }
        
        // Special case: if parent is root and now has only 1 child
        if (is_node_root(parent) && *get_internal_node_num_keys(parent) == 0) {
            // The merged node becomes the new root
            table->pager->root_page_num = left_sibling_page_num;
            set_node_root(left_sibling, true);
            *get_node_parent(left_sibling) = 0; // Root has no parent
            // Safe to free now since parent is being replaced
            free_page(table->pager, page_num);
        } else if (!is_node_root(parent) && *get_internal_node_num_keys(parent) < INTERNAL_NODE_MIN_KEYS) {
            // CRITICAL: Free BEFORE recursive call to avoid use-after-free
            free_page(table->pager, page_num);
            // Parent now has underflow - recursively handle it
            handle_internal_underflow(table, parent_page_num);
        } else {
            // No underflow, safe to free
            free_page(table->pager, page_num);
        }
        
    } else {
        // Must merge with right sibling (we are the leftmost child)
        uint32_t right_sibling_page_num = *get_internal_node_child(parent, child_index + 1);
        void* right_sibling = pager_get_page(table->pager, right_sibling_page_num);
        uint32_t right_cells = *get_leaf_node_num_cells(right_sibling);
        uint32_t current_cells = *get_leaf_node_num_cells(node);
        
        // Copy all cells from right sibling to current node
        for (uint32_t i = 0; i < right_cells; i++) {
            void* dest = get_leaf_node_cell(node, current_cells + i);
            void* src = get_leaf_node_cell(right_sibling, i);
            memcpy(dest, src, LEAF_NODE_CELL_SIZE);
        }
        
        *get_leaf_node_num_cells(node) = current_cells + right_cells;
        
        // Update next leaf pointer
        *get_leaf_node_next_leaf(node) = *get_leaf_node_next_leaf(right_sibling);
        
        // CRITICAL FIX: Update current node's max key in parent BEFORE shifting
        // Must do this before removing entries while indices are still correct
        uint32_t new_max = get_node_max_key(table->pager, node);
        *get_internal_node_key(parent, child_index) = new_max;
        
        // Remove right sibling's entry from parent
        // CRITICAL FIX: Use memmove for safe overlapping memory operations
        if (child_index + 1 < num_parent_keys - 1) {
            // Shift children
            memmove(get_internal_node_child(parent, child_index + 1),
                   get_internal_node_child(parent, child_index + 2),
                   (num_parent_keys - child_index - 2) * sizeof(uint32_t));
            // Shift keys
            memmove(get_internal_node_key(parent, child_index + 1),
                   get_internal_node_key(parent, child_index + 2),
                   (num_parent_keys - child_index - 2) * sizeof(uint32_t));
        }
        
        // Update right child
        if (child_index + 1 == num_parent_keys - 1) {
            // Right sibling was second to last, so right child becomes last child
            *get_internal_node_child(parent, num_parent_keys - 1) = *get_internal_node_right_child(parent);
        } else {
            *get_internal_node_child(parent, num_parent_keys - 1) = *get_internal_node_right_child(parent);
        }
        
        *get_internal_node_num_keys(parent) = num_parent_keys - 1;
        
        // CRITICAL FIX: After shifting, update all shifted keys to reflect their actual child max keys
        for (uint32_t i = child_index; i < *get_internal_node_num_keys(parent); i++) {
            uint32_t child_page = *get_internal_node_child(parent, i);
            void* child_node = pager_get_page(table->pager, child_page);
            if (child_node != nullptr) {
                uint32_t child_max = get_node_max_key(table->pager, child_node);
                *get_internal_node_key(parent, i) = child_max;
            }
        }
        
        // Special case: if parent is root and now has only 1 child
        if (is_node_root(parent) && *get_internal_node_num_keys(parent) == 0) {
            // The merged node becomes the new root
            table->pager->root_page_num = page_num;
            set_node_root(node, true);
            *get_node_parent(node) = 0; // Root has no parent
            // Safe to free now
            free_page(table->pager, right_sibling_page_num);
        } else if (!is_node_root(parent) && *get_internal_node_num_keys(parent) < INTERNAL_NODE_MIN_KEYS) {
            // CRITICAL: Free BEFORE recursive call
            free_page(table->pager, right_sibling_page_num);
            // Parent now has underflow - recursively handle it
            handle_internal_underflow(table, parent_page_num);
        } else {
            // No underflow, safe to free
            free_page(table->pager, right_sibling_page_num);
        }
    }
}

// Handle internal node underflow by borrowing or merging
void handle_internal_underflow(Table* table, uint32_t page_num) {
    // SAFETY: Comprehensive null guards
    if (table == nullptr || table->pager == nullptr) {
        cout << "Error: Null table or pager in handle_internal_underflow" << endl;
        return;
    }
    
    void* node = pager_get_page(table->pager, page_num);
    if (node == nullptr) {
        cout << "Error: Could not load page " << page_num << " in handle_internal_underflow" << endl;
        return;
    }
    
    uint32_t parent_page_num = *get_node_parent(node);
    
    // If this is root, no underflow handling needed
    if (is_node_root(node)) {
        return;
    }
    
    void* parent = pager_get_page(table->pager, parent_page_num);
    if (parent == nullptr) {
        cout << "Error: Could not load parent page " << parent_page_num << " in handle_internal_underflow" << endl;
        return;
    }
    
    int32_t child_index = find_child_index_in_parent(parent, page_num);
    
    if (child_index < 0) {
        cout << "Error: Could not find child in parent during internal underflow handling" << endl;
        return;
    }
    
    uint32_t num_parent_keys = *get_internal_node_num_keys(parent);
    uint32_t num_keys = *get_internal_node_num_keys(node);
    
    // Try to borrow from right sibling first
    if ((uint32_t)child_index < num_parent_keys) {
        uint32_t right_sibling_page_num;
        if ((uint32_t)child_index == num_parent_keys - 1) {
            right_sibling_page_num = *get_internal_node_right_child(parent);
        } else {
            right_sibling_page_num = *get_internal_node_child(parent, child_index + 1);
        }
        
        void* right_sibling = pager_get_page(table->pager, right_sibling_page_num);
        if (right_sibling != nullptr) {
            uint32_t right_keys = *get_internal_node_num_keys(right_sibling);
            
            // If right sibling has extra keys, borrow one
            if (right_keys > INTERNAL_NODE_MIN_KEYS) {
                // Move first child and key from right sibling to end of current node
                uint32_t borrowed_child = *get_internal_node_child(right_sibling, 0);
                uint32_t borrowed_key = *get_internal_node_key(right_sibling, 0);
                
                // Add to current node
                *get_internal_node_child(node, num_keys + 1) = *get_internal_node_right_child(node);
                *get_internal_node_right_child(node) = borrowed_child;
                *get_internal_node_key(node, num_keys) = get_node_max_key(table->pager, node);
                *get_internal_node_num_keys(node) = num_keys + 1;
                
                // Update parent pointer of borrowed child
                void* borrowed_child_node = pager_get_page(table->pager, borrowed_child);
                if (borrowed_child_node != nullptr) {
                    *get_node_parent(borrowed_child_node) = page_num;
                }
                
                // Shift keys and children in right sibling left
                // CRITICAL FIX: Use memmove for safe overlapping memory operations
                if (right_keys > 1) {
                    memmove(get_internal_node_child(right_sibling, 0),
                           get_internal_node_child(right_sibling, 1),
                           (right_keys - 1) * sizeof(uint32_t));
                    memmove(get_internal_node_key(right_sibling, 0),
                           get_internal_node_key(right_sibling, 1),
                           (right_keys - 1) * sizeof(uint32_t));
                }
                *get_internal_node_child(right_sibling, right_keys - 1) = *get_internal_node_right_child(right_sibling);
                *get_internal_node_num_keys(right_sibling) = right_keys - 1;
                
                // Update parent key
                uint32_t new_max = get_node_max_key(table->pager, node);
                *get_internal_node_key(parent, child_index) = new_max;
                
                return; // Successfully borrowed
            }
        }
    }
    
    // Try to borrow from left sibling
    if (child_index > 0) {
        uint32_t left_sibling_page_num = *get_internal_node_child(parent, child_index - 1);
        void* left_sibling = pager_get_page(table->pager, left_sibling_page_num);
        if (left_sibling != nullptr) {
            uint32_t left_keys = *get_internal_node_num_keys(left_sibling);
            
            // If left sibling has extra keys, borrow one
            if (left_keys > INTERNAL_NODE_MIN_KEYS) {
                // Make room at beginning of current node
                // CRITICAL FIX: Use memmove for safe overlapping memory operations
                if (num_keys > 0) {
                    memmove(get_internal_node_child(node, 1),
                           get_internal_node_child(node, 0),
                           (num_keys + 1) * sizeof(uint32_t));
                    memmove(get_internal_node_key(node, 1),
                           get_internal_node_key(node, 0),
                           num_keys * sizeof(uint32_t));
                }
                
                // Borrow last key and child from left sibling
                uint32_t borrowed_child = *get_internal_node_right_child(left_sibling);
                uint32_t borrowed_key = get_node_max_key(table->pager, left_sibling);
                
                *get_internal_node_child(node, 0) = borrowed_child;
                *get_internal_node_key(node, 0) = borrowed_key;
                *get_internal_node_num_keys(node) = num_keys + 1;
                
                // Update parent pointer of borrowed child
                void* borrowed_child_node = pager_get_page(table->pager, borrowed_child);
                if (borrowed_child_node != nullptr) {
                    *get_node_parent(borrowed_child_node) = page_num;
                }
                
                // Update left sibling
                *get_internal_node_right_child(left_sibling) = *get_internal_node_child(left_sibling, left_keys - 1);
                *get_internal_node_num_keys(left_sibling) = left_keys - 1;
                
                // Update parent key for left sibling
                uint32_t new_left_max = get_node_max_key(table->pager, left_sibling);
                *get_internal_node_key(parent, child_index - 1) = new_left_max;
                
                return; // Successfully borrowed
            }
        }
    }
    
    // Cannot borrow - must merge with a sibling
    // Prefer merging with left sibling if it exists
    if (child_index > 0) {
        // Merge with left sibling
        uint32_t left_sibling_page_num = *get_internal_node_child(parent, child_index - 1);
        void* left_sibling = pager_get_page(table->pager, left_sibling_page_num);
        uint32_t left_keys = *get_internal_node_num_keys(left_sibling);
        
        // Copy all keys and children from current node to left sibling
        for (uint32_t i = 0; i < num_keys; i++) {
            *get_internal_node_child(left_sibling, left_keys + 1 + i) = *get_internal_node_child(node, i);
            *get_internal_node_key(left_sibling, left_keys + i) = *get_internal_node_key(node, i);
            
            // Update parent pointers
            void* child = pager_get_page(table->pager, *get_internal_node_child(node, i));
            if (child != nullptr) {
                *get_node_parent(child) = left_sibling_page_num;
            }
        }
        
        // Copy right child
        *get_internal_node_child(left_sibling, left_keys + num_keys + 1) = *get_internal_node_right_child(node);
        void* right_child = pager_get_page(table->pager, *get_internal_node_right_child(node));
        if (right_child != nullptr) {
            *get_node_parent(right_child) = left_sibling_page_num;
        }
        
        *get_internal_node_num_keys(left_sibling) = left_keys + num_keys;
        *get_internal_node_right_child(left_sibling) = *get_internal_node_right_child(node);
        
        // CRITICAL FIX: Update left sibling's max key in parent BEFORE shifting
        // This must be done before we remove entries, while indices are still correct
        uint32_t new_left_max = get_node_max_key(table->pager, left_sibling);
        if (child_index > 0) {
            *get_internal_node_key(parent, child_index - 1) = new_left_max;
        }
        
        // Remove current node's entry from parent
        for (uint32_t i = child_index; i < num_parent_keys - 1; i++) {
            *get_internal_node_child(parent, i) = *get_internal_node_child(parent, i + 1);
            *get_internal_node_key(parent, i) = *get_internal_node_key(parent, i + 1);
        }
        
        if ((uint32_t)child_index == num_parent_keys - 1) {
            *get_internal_node_child(parent, child_index) = *get_internal_node_right_child(parent);
        } else {
            *get_internal_node_child(parent, num_parent_keys - 1) = *get_internal_node_right_child(parent);
        }
        
        *get_internal_node_num_keys(parent) = num_parent_keys - 1;
        
        // Special case: if parent is root and now empty
        if (is_node_root(parent) && *get_internal_node_num_keys(parent) == 0) {
            table->pager->root_page_num = left_sibling_page_num;
            set_node_root(left_sibling, true);
            *get_node_parent(left_sibling) = 0;
            // Safe to free now
            free_page(table->pager, page_num);
        } else if (!is_node_root(parent) && *get_internal_node_num_keys(parent) < INTERNAL_NODE_MIN_KEYS) {
            // CRITICAL: Free BEFORE recursive call
            free_page(table->pager, page_num);
            // Recursively handle parent underflow
            handle_internal_underflow(table, parent_page_num);
        } else {
            // No underflow, safe to free
            free_page(table->pager, page_num);
        }
    } else {
        // Merge with right sibling (we are the leftmost child)
        uint32_t right_sibling_page_num = *get_internal_node_child(parent, child_index + 1);
        void* right_sibling = pager_get_page(table->pager, right_sibling_page_num);
        uint32_t right_keys = *get_internal_node_num_keys(right_sibling);
        
        // Copy all keys and children from right sibling to current node
        for (uint32_t i = 0; i < right_keys; i++) {
            *get_internal_node_child(node, num_keys + 1 + i) = *get_internal_node_child(right_sibling, i);
            *get_internal_node_key(node, num_keys + i) = *get_internal_node_key(right_sibling, i);
            
            // Update parent pointers
            void* child = pager_get_page(table->pager, *get_internal_node_child(right_sibling, i));
            if (child != nullptr) {
                *get_node_parent(child) = page_num;
            }
        }
        
        *get_internal_node_child(node, num_keys + right_keys + 1) = *get_internal_node_right_child(right_sibling);
        void* rc = pager_get_page(table->pager, *get_internal_node_right_child(right_sibling));
        if (rc != nullptr) {
            *get_node_parent(rc) = page_num;
        }
        
        *get_internal_node_num_keys(node) = num_keys + right_keys;
        *get_internal_node_right_child(node) = *get_internal_node_right_child(right_sibling);
        
        // CRITICAL FIX: Update current node's max key in parent BEFORE shifting
        // This must be done before we remove entries, while indices are still correct
        uint32_t new_max = get_node_max_key(table->pager, node);
        *get_internal_node_key(parent, child_index) = new_max;
        
        // Remove right sibling's entry from parent
        for (uint32_t i = child_index + 1; i < num_parent_keys - 1; i++) {
            *get_internal_node_child(parent, i) = *get_internal_node_child(parent, i + 1);
            *get_internal_node_key(parent, i) = *get_internal_node_key(parent, i + 1);
        }
        
        if (child_index + 1 == num_parent_keys - 1) {
            *get_internal_node_child(parent, num_parent_keys - 1) = *get_internal_node_right_child(parent);
        } else {
            *get_internal_node_child(parent, num_parent_keys - 1) = *get_internal_node_right_child(parent);
        }
        
        *get_internal_node_num_keys(parent) = num_parent_keys - 1;
        
        // Special case: if parent is root and now empty
        if (is_node_root(parent) && *get_internal_node_num_keys(parent) == 0) {
            table->pager->root_page_num = page_num;
            set_node_root(node, true);
            *get_node_parent(node) = 0;
            // Safe to free now
            free_page(table->pager, right_sibling_page_num);
        } else if (!is_node_root(parent) && *get_internal_node_num_keys(parent) < INTERNAL_NODE_MIN_KEYS) {
            // CRITICAL: Free BEFORE recursive call
            free_page(table->pager, right_sibling_page_num);
            // Recursively handle parent underflow
            handle_internal_underflow(table, parent_page_num);
        } else {
            // No underflow, safe to free
            free_page(table->pager, right_sibling_page_num);
        }
    }
}

// --- Helper function to print a row  ---
void print_row(Row* row) {
    cout << "(" << row->id << ", "
         << row->username << ", "
         << row->email << ")" << endl;
}

// --- Helper struct for key lookup ---
struct KeyLocation {
    Cursor* cursor;
    void* node;
    bool key_found;
};

// Helper function to find a key and check if it exists
KeyLocation find_key_location(Table* table, uint32_t key) {
    Cursor* cursor = table_find(table, key);
    if (cursor == nullptr) {
        return {nullptr, nullptr, false};
    }
    void* node = pager_get_page(table->pager, cursor->page_num);
    
    bool found = false;
    if (node != nullptr) {
        uint32_t num_cells = *get_leaf_node_num_cells(node);
        if (cursor->cell_num < num_cells) {
            uint32_t key_at_index = *get_leaf_node_key(node, cursor->cell_num);
            if (key_at_index == key) {
                found = true;
            }
        }
    }
    
    return {cursor, node, found};
}

// --- Executor Functions ---
// (No changes needed, uses table_find which uses pager->root_page_num)
ExecuteResult execute_insert(Statement* statement, Table* table) {
    // SAFETY: Comprehensive null guards
    if (statement == nullptr || table == nullptr) {
        cout << "Error: Null statement or table in execute_insert" << endl;
        return EXECUTE_PAGE_OUT_OF_BOUNDS;
    }
    
    Row* row_to_insert = &(statement->row_to_insert);
    uint32_t key = row_to_insert->id;

    KeyLocation loc = find_key_location(table, key);
    
    if (loc.cursor == nullptr || loc.node == nullptr) {
        if (loc.cursor) delete loc.cursor;
        return EXECUTE_PAGE_OUT_OF_BOUNDS;
    }
    
    if (loc.key_found) {
        delete loc.cursor;
        return EXECUTE_DUPLICATE_KEY;
    }

    uint32_t num_cells = *get_leaf_node_num_cells(loc.node);
    if (num_cells >= LEAF_NODE_MAX_CELLS) {
        leaf_node_split_and_insert(loc.cursor, key, row_to_insert);
        delete loc.cursor;
        return EXECUTE_SUCCESS;
    }

    leaf_node_insert(loc.cursor, key, row_to_insert);
    delete loc.cursor;
    return EXECUTE_SUCCESS;
}

ExecuteResult execute_select(Statement* statement, Table* table) {
    // SAFETY: Comprehensive null guards
    if (table == nullptr) {
        cout << "Error: Null table in execute_select" << endl;
        return EXECUTE_PAGE_OUT_OF_BOUNDS;
    }
    
    Cursor* cursor = table_start(table);
    if (cursor == nullptr) {
        cout << "Error: Could not create cursor in execute_select" << endl;
        return EXECUTE_PAGE_OUT_OF_BOUNDS;
    }
    Row row;
    uint32_t count = 0;

    while(!cursor->end_of_table) {
        void* leaf_node = pager_get_page(table->pager, cursor->page_num);
        if (leaf_node == nullptr) {
            delete cursor;
            return EXECUTE_PAGE_OUT_OF_BOUNDS;
        }
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
    // SAFETY: Comprehensive null guards
    if (statement == nullptr || table == nullptr) {
        cout << "Error: Null statement or table in execute_find" << endl;
        return EXECUTE_PAGE_OUT_OF_BOUNDS;
    }
    
    uint32_t key_to_find = statement->row_to_insert.id;
    KeyLocation loc = find_key_location(table, key_to_find);

    if (loc.cursor == nullptr || loc.node == nullptr) {
        if (loc.cursor) delete loc.cursor;
        return EXECUTE_PAGE_OUT_OF_BOUNDS;
    }

    if (loc.key_found) {
        Row row;
        deserialize_row(get_leaf_node_value(loc.node, loc.cursor->cell_num), &row);
        print_row(&row);
        delete loc.cursor;
        return EXECUTE_SUCCESS;
    }

    delete loc.cursor;
    return EXECUTE_RECORD_NOT_FOUND;
}

ExecuteResult execute_delete(Statement* statement, Table* table) {
    // SAFETY: Comprehensive null guards
    if (statement == nullptr || table == nullptr) {
        cout << "Error: Null statement or table in execute_delete" << endl;
        return EXECUTE_PAGE_OUT_OF_BOUNDS;
    }
    
    uint32_t key_to_delete = statement->row_to_insert.id;
    KeyLocation loc = find_key_location(table, key_to_delete);

    if (loc.cursor == nullptr || loc.node == nullptr) {
        if (loc.cursor) delete loc.cursor;
        return EXECUTE_PAGE_OUT_OF_BOUNDS;
    }

    if (loc.key_found) {
        leaf_node_delete(loc.cursor);
        delete loc.cursor;
        return EXECUTE_SUCCESS;
    }

    delete loc.cursor;
    return EXECUTE_RECORD_NOT_FOUND;
}

ExecuteResult execute_update(Statement* statement, Table* table) {
    // SAFETY: Comprehensive null guards
    if (statement == nullptr || table == nullptr) {
        cout << "Error: Null statement or table in execute_update" << endl;
        return EXECUTE_PAGE_OUT_OF_BOUNDS;
    }
    
    uint32_t key_to_update = statement->row_to_insert.id;
    KeyLocation loc = find_key_location(table, key_to_update);

    if (loc.cursor == nullptr || loc.node == nullptr) {
        if (loc.cursor) delete loc.cursor;
        return EXECUTE_PAGE_OUT_OF_BOUNDS;
    }

    if (loc.key_found) {
        Row* row_to_update = &(statement->row_to_insert);
        serialize_row(row_to_update, get_leaf_node_value(loc.node, loc.cursor->cell_num));
        delete loc.cursor;
        return EXECUTE_SUCCESS;
    }

    delete loc.cursor;
    return EXECUTE_RECORD_NOT_FOUND;
}

ExecuteResult execute_range(Statement* statement, Table* table) {
    // SAFETY: Comprehensive null guards
    if (statement == nullptr || table == nullptr) {
        cout << "Error: Null statement or table in execute_range" << endl;
        return EXECUTE_PAGE_OUT_OF_BOUNDS;
    }
    
    uint32_t start_key = statement->range_start;
    uint32_t end_key = statement->range_end;

    if (start_key > end_key) {
        cout << "Error: Invalid range (start > end)" << endl;
        return EXECUTE_SUCCESS;
    }

    Cursor* cursor = table_find(table, start_key);
    if (cursor == nullptr) {
        cout << "Error: Could not create cursor in execute_range" << endl;
        return EXECUTE_PAGE_OUT_OF_BOUNDS;
    }
    Row row;
    uint32_t count = 0;

    while(!cursor->end_of_table) {
        void* leaf_node = pager_get_page(table->pager, cursor->page_num);
        if (leaf_node == nullptr) {
            delete cursor;
            return EXECUTE_PAGE_OUT_OF_BOUNDS;
        }
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

// SAFETY: Tree validation function with asserts
bool validate_tree_node(Pager* pager, uint32_t page_num, uint32_t* min_key, uint32_t* max_key, int* depth, bool is_root_call) {
    if (pager == nullptr) {
        cout << "ERROR: Null pager in validate_tree_node" << endl;
        return false;
    }
    
    void* node = pager_get_page(pager, page_num);
    if (node == nullptr) {
        cout << "ERROR: Cannot load page " << page_num << endl;
        return false;
    }
    
    if (get_node_type(node) == NODE_LEAF) {
        uint32_t num_cells = *get_leaf_node_num_cells(node);
        
        // Check cell count bounds
        if (num_cells > LEAF_NODE_MAX_CELLS) {
            cout << "ERROR: Leaf page " << page_num << " has too many cells: " << num_cells << endl;
            return false;
        }
        
        // Check minimum cells (except for root)
        if (!is_node_root(node) && num_cells < LEAF_NODE_MIN_CELLS) {
            cout << "ERROR: Non-root leaf page " << page_num << " has too few cells: " << num_cells << " (min: " << LEAF_NODE_MIN_CELLS << ")" << endl;
            return false;
        }
        
        // Check keys are sorted
        for (uint32_t i = 1; i < num_cells; i++) {
            uint32_t prev_key = *get_leaf_node_key(node, i - 1);
            uint32_t curr_key = *get_leaf_node_key(node, i);
            if (prev_key >= curr_key) {
                cout << "ERROR: Leaf page " << page_num << " has unsorted keys at index " << i - 1 << "-" << i << ": " << prev_key << " >= " << curr_key << endl;
                return false;
            }
        }
        
        // Set min/max for this leaf
        if (num_cells > 0) {
            if (min_key) *min_key = *get_leaf_node_key(node, 0);
            if (max_key) *max_key = *get_leaf_node_key(node, num_cells - 1);
        }
        
        *depth = 0;
        return true;
        
    } else { // Internal node
        uint32_t num_keys = *get_internal_node_num_keys(node);
        
        // Check key count bounds
        if (num_keys > INTERNAL_NODE_MAX_KEYS) {
            cout << "ERROR: Internal page " << page_num << " has too many keys: " << num_keys << endl;
            return false;
        }
        
        // Check minimum keys (except for root)
        if (!is_node_root(node) && num_keys < INTERNAL_NODE_MIN_KEYS) {
            cout << "ERROR: Non-root internal page " << page_num << " has too few keys: " << num_keys << " (min: " << INTERNAL_NODE_MIN_KEYS << ")" << endl;
            return false;
        }
        
        // Validate all children and check uniform depth
        int first_child_depth = -1;
        uint32_t prev_max = 0;
        
        for (uint32_t i = 0; i <= num_keys; i++) {
            uint32_t child_page_num = (i == num_keys) ? 
                *get_internal_node_right_child(node) : 
                *get_internal_node_child(node, i);
            
            uint32_t child_min, child_max;
            int child_depth;
            
            if (!validate_tree_node(pager, child_page_num, &child_min, &child_max, &child_depth, false)) {
                return false;
            }
            
            // Check uniform depth
            if (first_child_depth == -1) {
                first_child_depth = child_depth;
            } else if (child_depth != first_child_depth) {
                cout << "ERROR: Internal page " << page_num << " has children at different depths" << endl;
                return false;
            }
            
            // Check keys are sorted relative to children
            if (i > 0 && child_min <= prev_max) {
                cout << "ERROR: Internal page " << page_num << " has overlapping child ranges" << endl;
                return false;
            }
            
            prev_max = child_max;
            
            // For non-rightmost children, verify the key separator
            if (i < num_keys) {
                uint32_t separator_key = *get_internal_node_key(node, i);
                if (separator_key < child_max) {
                    cout << "ERROR: Internal page " << page_num << " key[" << i << "]=" << separator_key << " is less than child max=" << child_max << endl;
                    return false;
                }
            }
        }
        
        if (min_key) *min_key = prev_max; // Will be set by first child
        if (max_key) *max_key = prev_max;
        *depth = first_child_depth + 1;
        return true;
    }
}

void validate_tree(Table* table) {
    if (table == nullptr || table->pager == nullptr) {
        cout << "ERROR: Null table or pager in validate_tree" << endl;
        return;
    }
    
    cout << "=== Validating B-Tree ===" << endl;
    uint32_t min_key, max_key;
    int depth;
    
    bool valid = validate_tree_node(table->pager, table->pager->root_page_num, &min_key, &max_key, &depth, true);
    
    if (valid) {
        cout << "✅ Tree is valid! Depth: " << depth << endl;
    } else {
        cout << "❌ Tree validation FAILED!" << endl;
    }
}

// NEW: Tree visualization functions
void print_node(Pager* pager, uint32_t page_num, uint32_t indent_level) {
    void* node = pager_get_page(pager, page_num);
    if (node == nullptr) {
        for (uint32_t i = 0; i < indent_level; i++) {
            cout << "  ";
        }
        cout << "- ERROR: Cannot access page " << page_num << endl;
        return;
    }
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

    cout << "Enhanced SQLite Clone - Commands: .exit | .btree | .validate" << endl;

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
            case (EXECUTE_DISK_ERROR):
                cout << "Error: Disk I/O error. Check disk space and permissions." << endl;
                break;
            case (EXECUTE_PAGE_OUT_OF_BOUNDS):
                cout << "Error: Page out of bounds. Database may be too large." << endl;
                break;
        }
    }
}