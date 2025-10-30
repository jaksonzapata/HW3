/*
Assignment:
HW3 - Parser and Code Generator for PL/0
Author(s): Jacob Smith, Jackson Zapata
Language: C (only)

To Compile:
Scanner:
gcc -O2 -std=c11 -o lex lex.c

Parser/Code Generator:
gcc -O2 -std=c11 -o parsercodegen parsercodegen.c

To Execute (on Eustis):
./lex <input_file.txt>
./parsercodegen

where:
<input_file.txt> is the path to the PL/0 source program

Notes:
- lex.c accepts ONE command-line argument (input PL/0 source file)
- parsercodegen.c accepts NO command-line arguments
- Input filename is hard-coded in parsercodegen.c
- Implements recursive-descent parser for PL/0 grammar
- Generates PM/0 assembly code (see Appendix A for ISA)
- All development and testing performed on Eustis

Class: COP3402 - System Software - Fall 2025
Instructor: Dr. Jie Lin
Due Date: Friday, October 31, 2025 at 11:59 PM ET
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_SYMBOL_TABLE_SIZE 500
#define MAX_CODE_LENGTH 500
#define MAX_LEXEME_LEN 256

// Token types (matching lex.c)
typedef enum {
    skipsym = 1, identsym, numbersym, plussym, minussym,
    multsym, slashsym, eqsym, neqsym, lessym, leqsym,
    gtrsym, geqsym, lparentsym, rparentsym, commasym,
    semicolonsym, periodsym, becomessym, beginsym, endsym,
    ifsym, fisym, thensym, whilesym, dosym, callsym,
    constsym, varsym, procsym, writesym, readsym, elsesym, evensym
} TokenType;

// Symbol table structure
typedef struct {
    int kind;      // const = 1, var = 2, proc = 3
    char name[12]; // name up to 11 chars
    int val;       // number (ASCII value)
    int level;     // L level
    int addr;      // M address
    int mark;      // to indicate unavailable or deleted
} symbol;

// Instruction structure
typedef struct {
    int op;  // opcode
    int l;   // lexicographical level
    int m;   // modifier
} instruction;

// Global variables
symbol symbol_table[MAX_SYMBOL_TABLE_SIZE];
instruction code[MAX_CODE_LENGTH];
int symbol_table_index = 0;
int code_index = 0;
int current_token;
char current_identifier[MAX_LEXEME_LEN];
int current_number;
FILE *token_file;

// Function prototypes
void error(const char *msg);
void get_next_token();
void emit(int op, int l, int m);
int symbol_table_check(const char *name);
void program();
void block();
void const_declaration();
int var_declaration();
void statement();


void term();
void factor();
void print_assembly();
void write_elf_file();

// Opcode names for display
const char* op_names[] = {
    "", "LIT", "OPR", "LOD", "STO", "CAL", "INC", "JMP", "JPC", "SYS"
};

// Main function
int main() 
{
    token_file = fopen("tokens.txt", "r");
    if (!token_file) 
    {
        fprintf(stderr, "Error: Cannot open tokens.txt\n");
        return 1;
    }

    // Get first token
    get_next_token();
    
    // Check for scanning errors (skipsym present)
    if (current_token == skipsym) 
    {
        error("Scanning error detected by lexer (skipsym present)");
    }

    // Emit initial JMP to main code
    emit(7, 0, 0); // JMP 0 0 - will be patched later

    // Parse program
    program();

    fclose(token_file);

    // Print assembly to terminal
    print_assembly();

    // Write to elf.txt
    write_elf_file();

    return 0;
}

// Error handling
void error(const char *msg) 
{
    printf("Error: %s\n", msg);
    
    FILE *elf = fopen("elf.txt", "w");
    if (elf) {
        fprintf(elf, "Error: %s\n", msg);
        fclose(elf);
    }
    
    exit(1);
}

// Get next token from file
void get_next_token() 
{
    if (fscanf(token_file, "%d", &current_token) != 1) 
    {
        current_token = -1; // EOF marker
        return;
    }

    // Check for scanning error
    if (current_token == skipsym) 
    {
        return;
    }

    // If identifier or number, read the lexeme/value
    if (current_token == identsym) 
    {
        if (fscanf(token_file, "%s", current_identifier) != 1) 
        {
            error("Expected identifier name");
        }
    } else if (current_token == numbersym) 
    {
        if (fscanf(token_file, "%d", &current_number) != 1) 
        {
            error("Expected number value");
        }
    }
}

// Emit instruction
void emit(int op, int l, int m) 
{
    if (code_index >= MAX_CODE_LENGTH) 
    {
        error("Code segment overflow");
    }
    code[code_index].op = op;
    code[code_index].l = l;
    code[code_index].m = m;
    code_index++;
}

// Symbol table lookup
int symbol_table_check(const char *name) 
{
    for (int i = 0; i < symbol_table_index; i++) 
    {
        if (strcmp(symbol_table[i].name, name) == 0 && symbol_table[i].mark == 0) 
        {
            return i;
        }
    }
    return -1;
}

// PROGRAM ::= BLOCK "."
void program() 
{
    block();
    
    if (current_token != periodsym) 
    {
        error("program must end with period");
    }
    
    emit(9, 0, 3); // SYS 0 3 (HALT)
    
    // Patch initial JMP
    code[0].m = code_index;
}

// BLOCK ::= CONST-DECLARATION VAR-DECLARATION STATEMENT
void block() 
{
    int jmp_addr = code_index;
    emit(7, 0, 0); // JMP - will be patched
    
    int saved_table_index = symbol_table_index;
    
    const_declaration();
    int num_vars = var_declaration();
    
    // Patch JMP to skip declarations
    code[jmp_addr].m = code_index;
    
    emit(6, 0, 3 + num_vars); // INC 0 (3 + num_vars)
    
    statement();
    
    // Mark symbols as unavailable
    symbol_table_index = saved_table_index;
}

// CONST-DECLARATION
void const_declaration() 
{
    if (current_token == constsym) 
    {
        do {
            get_next_token();
            
            if (current_token != identsym) 
            {
                error("const, var, and read keywords must be followed by identifier");
            }
            
            char saved_name[12];
            strcpy(saved_name, current_identifier);
            
            if (symbol_table_check(saved_name) != -1) 
            {
                error("symbol name has already been declared");
            }
            
            get_next_token();
            
            if (current_token != eqsym) 
            {
                error("constants must be assigned with =");
            }
            
            get_next_token();
            
            if (current_token != numbersym) 
            {
                error("constants must be assigned an integer value");
            }
            
            // Add to symbol table
            symbol_table[symbol_table_index].kind = 1;
            strcpy(symbol_table[symbol_table_index].name, saved_name);
            symbol_table[symbol_table_index].val = current_number;
            symbol_table[symbol_table_index].level = 0;
            symbol_table[symbol_table_index].addr = 0;
            symbol_table[symbol_table_index].mark = 0;
            symbol_table_index++;
            
            get_next_token();
            
        } while (current_token == commasym);
        
        if (current_token != semicolonsym) 
        {
            error("constant and variable declarations must be followed by a semicolon");
        }
        
        get_next_token();
    }
}

// VAR-DECLARATION - returns number of variables
int var_declaration() 
{
    int num_vars = 0;
    
    if (current_token == varsym) {
        do {
            num_vars++;
            get_next_token();
            
            if (current_token != identsym) 
            {
                error("const, var, and read keywords must be followed by identifier");
            }
            
            if (symbol_table_check(current_identifier) != -1) 
            {
                error("symbol name has already been declared");
            }
            
            // Add to symbol table
            symbol_table[symbol_table_index].kind = 2;
            strcpy(symbol_table[symbol_table_index].name, current_identifier);
            symbol_table[symbol_table_index].val = 0;
            symbol_table[symbol_table_index].level = 0;
            symbol_table[symbol_table_index].addr = num_vars + 2;
            symbol_table[symbol_table_index].mark = 0;
            symbol_table_index++;
            
            get_next_token();
            
        } while (current_token == commasym);
        
        if (current_token != semicolonsym) 
        {
            error("constant and variable declarations must be followed by a semicolon");
        }
        
        get_next_token();
    }
    
    return num_vars;
}

// STATEMENT
void statement() 
{
    if (current_token == identsym) 
    {
        char saved_name[12];
        strcpy(saved_name, current_identifier);
        
        int sym_idx = symbol_table_check(saved_name);
        if (sym_idx == -1) 
        {
            error("undeclared identifier");
        }
        
        if (symbol_table[sym_idx].kind != 2) 
        {
            error("only variable values may be altered");
        }
        
        get_next_token();
        
        if (current_token != becomessym) 
        {
            error("assignment statements must use :=");
        }
        
        get_next_token();
        expression();
        
        emit(4, 0, symbol_table[sym_idx].addr); // STO
        symbol_table[sym_idx].mark = 1;
        return;
    }
    
    if (current_token == beginsym) 
    {
        do {
            get_next_token();
            statement();
        } while (current_token == semicolonsym);
        
        if (current_token != endsym) 
        {
            error("begin must be followed by end");
        }
        
        get_next_token();
        return;
    }
    
    if (current_token == ifsym) 
    {
        get_next_token();
        condition();
        
        int jpc_idx = code_index;
        emit(8, 0, 0); // JPC - will be patched
        
        if (current_token != thensym) 
        {
            error("if must be followed by then");
        }
        
        get_next_token();
        statement();
        
        if (current_token != fisym) 
        {
            error("if must be followed by then");
        }
        
        get_next_token();
        
        code[jpc_idx].m = code_index;
        return;
    }
    
    if (current_token == whilesym) 
    {
        get_next_token();
        int loop_idx = code_index;
        
        condition();
        
        if (current_token != dosym) 
        {
            error("while must be followed by do");
        }
        
        get_next_token();
        
        int jpc_idx = code_index;
        emit(8, 0, 0); // JPC - will be patched
        
        statement();
        
        emit(7, 0, loop_idx); // JMP back to condition
        code[jpc_idx].m = code_index;
        return;
    }
    
    if (current_token == readsym) 
    {
        get_next_token();
        
        if (current_token != identsym) 
        {
            error("const, var, and read keywords must be followed by identifier");
        }
        
        int sym_idx = symbol_table_check(current_identifier);
        if (sym_idx == -1) 
        {
            error("undeclared identifier");
        }
        
        if (symbol_table[sym_idx].kind != 2) 
        {
            error("only variable values may be altered");
        }
        
        get_next_token();
        
        emit(9, 0, 2); // SYS 0 2 (READ)
        emit(4, 0, symbol_table[sym_idx].addr); // STO
        symbol_table[sym_idx].mark = 1;
        return;
    }
    
    if (current_token == writesym) 
    {
        get_next_token();
        expression();
        emit(9, 0, 1); // SYS 0 1 (WRITE)
        return;
    }
}

// TODO: FOR TEAMMATE TO IMPLEMENT
// CONDITION ::= "even" EXPRESSION | EXPRESSION REL-OP EXPRESSION
void condition() {
    // TEAMMATE: Implement this function following the grammar
    // Hint: Check for evensym first, then handle relational operators
    // Use expression() to parse expressions
    // Emit appropriate OPR instructions for comparisons
    
    // Placeholder error - remove when implementing
    error("condition() not yet implemented by teammate");
}

// TODO: FOR TEAMMATE TO IMPLEMENT  
// EXPRESSION ::= TERM { ("+" | "-") TERM }
void expression() {
    // TEAMMATE: Implement this function following the grammar
    // Hint: Call term() first, then loop while plussym or minussym
    // Emit OPR 0 1 for ADD, OPR 0 2 for SUB
    
    // Placeholder error - remove when implementing
    error("expression() not yet implemented by teammate");
}

// TERM ::= FACTOR { ("*" | "/") FACTOR }
void term() 
{
    factor();
    
    while (current_token == multsym || current_token == slashsym) 
    {
        if (current_token == multsym) 
        {
            get_next_token();
            factor();
            emit(2, 0, 3); // OPR 0 3 (MUL)
        } else 
        {
            get_next_token();
            factor();
            emit(2, 0, 4); // OPR 0 4 (DIV)
        }
    }
}

// FACTOR ::= IDENT | NUMBER | "(" EXPRESSION ")"
void factor() {
    if (current_token == identsym) 
    {
        int sym_idx = symbol_table_check(current_identifier);
        if (sym_idx == -1) 
        {
            error("undeclared identifier");
        }
        
        if (symbol_table[sym_idx].kind == 1) 
        {
            // Constant
            emit(1, 0, symbol_table[sym_idx].val); // LIT
        } else if (symbol_table[sym_idx].kind == 2) 
        {
            // Variable
            emit(3, 0, symbol_table[sym_idx].addr); // LOD
            symbol_table[sym_idx].mark = 1;
        }
        
        get_next_token();
    } else if (current_token == numbersym) 
    {
        emit(1, 0, current_number); // LIT
        get_next_token();
    } else if (current_token == lparentsym) 
    {
        get_next_token();
        expression();
        
        if (current_token != rparentsym) 
        {
            error("right parenthesis must follow left parenthesis");
        }
        
        get_next_token();
    } else 
    {
        error("arithmetic equations must contain operands, parentheses, numbers, or symbols");
    }
}

// Print assembly code to terminal
void print_assembly() 
{
    printf("\nAssembly Code:\n");
    printf("Line\tOP\tL\tM\n");
    
    for (int i = 1; i < code_index; i++) 
    {
        printf("%d\t%s\t%d\t%d\n", i - 1, op_names[code[i].op], code[i].l, code[i].m);
    }
    
    printf("\nSymbol Table:\n");
    printf("Kind | Name\t\t | Value | Level | Address | Mark\n");
    printf("---------------------------------------------------\n");
    
    for (int i = 0; i < symbol_table_index; i++) 
    {
        printf("%d    | %-11s | %d     | %d     | %d       | %d\n",
               symbol_table[i].kind,
               symbol_table[i].name,
               symbol_table[i].val,
               symbol_table[i].level,
               symbol_table[i].addr,
               symbol_table[i].mark);
    }
}

// Write to elf.txt
void write_elf_file() 
{
    FILE *elf = fopen("elf.txt", "w");
    if (!elf) {
        fprintf(stderr, "Error: Cannot create elf.txt\n");
        return;
    }
    
    for (int i = 1; i < code_index; i++) {
        fprintf(elf, "%d %d %d\n", code[i].op, code[i].l, code[i].m);
    }
    
    fclose(elf);
}