import re
import argparse

class PseudocodeHandler:
    
    def __init__(self):
        # Token specification
        self.token_dict = [
            ('INT',       r'int\b'),        # Integer datatype keyword
            ('FLOAT',     r'float\b'),      # Float datatype keyword
            ('RETURN',    r'return\b'),     # Return keyword
            ('WHILE',     r'while\b'),      # While loop keyword
            ('NEXT',      r'next\b'),       # Next keyword - works like a pointer increment to move to next output struct
            ('ID',        r'[a-zA-Z_][a-zA-Z0-9_]*(?:\.[a-zA-Z_][a-zA-Z0-9_]*)*'),  # Identifier
            # ('ID',        r'[a-zA-Z_][a-zA-Z0-9_]*'),  # Identifier
            ('NUM_INT',   r'\d+'),          # Integer number
            ('NUM_FLOAT', r'\d+\.\d*'),     # Float number
            ('LOGICAL_AND', r'\&\&'),       # Logical AND
            ('LOGICAL_OR', r'\|\|'),        # Logical OR
            ('LOGICAL_NOT', r'\!'),         # Logical NOT
            ('SHIFT_LEFT', r'\<\<'),        # Shift left
            ('SHIFT_RIGHT', r'\>\>'),       # Shift right
            ('BITWISE_AND', r'\&'),         # Bitwise AND
            ('BITWISE_OR', r'\|'),          # Bitwise OR
            ('BITWISE_XOR', r'\^'),         # Bitwise XOR
            ('BITWISE_NOT', r'\~'),         # Bitwise NOT
            ('INC_OP',    r'\+\+'),         # Increment operator
            ('DEC_OP',    r'\-\-'),         # Decrement operator
            ('ADD_OP',    r'\+'),           # Addition operator
            ('SUB_OP',    r'\-'),           # Subtraction operator
            ('MUL_OP',    r'\*'),           # Multiplication operator
            ('DIV_OP',    r'/'),            # Division operator
            ('MOD_OP',    r'%'),            # Modulus operator
            ('REL_OP',     r'==|!=|<=|>=|<|>'),  # Relational operators
            ('ASSIGN',    r'='),            # Assignment operator
            ('SEMI',      r';'),            # Semicolon
            ('COMMA',     r','),            # Comma
            ('LPAREN',    r'\('),           # Left Parenthesis
            ('RPAREN',    r'\)'),           # Right Parenthesis
            ('LBRACE',    r'\{'),           # Left Brace
            ('RBRACE',    r'\}'),           # Right Brace
            ('LBRACK',    r'\['),           # Left Bracket
            ('RBRACK',    r'\]'),           # Right Bracket
            ('WHITESPACE',r'[ \t]+'),       # Whitespace (ignored)
            ('NEWLINE',   r'\n'),           # Line breaks
        ]

    def lexer(self, code):
        tokens_re = '|'.join('(?P<%s>%s)' % pair for pair in self.token_dict)
        for mo in re.finditer(tokens_re, code):
            kind = mo.lastgroup
            value = mo.group()
            if kind == 'WHITESPACE' or kind == 'NEWLINE':
                continue
            elif kind in ('NUM_INT', 'NUM_FLOAT'):
                value = float(value) if '.' in value else int(value)
            yield kind, value

    def generate_cpp_code(self, tokens, substitutions={}):
        code = ""
        indent_level = 0

        # Helper function to manage indentation
        def add_indentation(level):
            return "    " * level  # 4 spaces per indent level

        i = 0
        while i < len(tokens):
            token_type, token_value = tokens[i]
            if token_type == "LBRACE":
                # Add opening brace and increase indentation level
                code += " {\n"
                indent_level += 1
                code += add_indentation(indent_level)
            elif token_type == "RBRACE":
                # Decrease indentation level and add closing brace
                if indent_level > 0:
                    indent_level -= 1
                code += add_indentation(indent_level) + "}\n"
                if (i + 1) < len(tokens) and tokens[i + 1][0] != "RBRACE":
                    code += add_indentation(indent_level)
            elif token_type == "SEMI":
                # Add semicolon
                code += ";\n"
                if (i + 1) < len(tokens) and tokens[i + 1][0] != "RBRACE":
                    code += add_indentation(indent_level)
            elif token_type in ["WHILE", "INT", "FLOAT", "RETURN"]:
                # Ensure keywords start on a new line with a space following them
                code += token_value + " "
            else:
                # Apply substitutions to IDs
                token_value = str(token_value)
                for subs in substitutions:
                    # Use regex to replace the ID with the value
                    token_value = re.sub(subs, substitutions[subs], token_value)
                # Append other tokens directly
                code += token_value
            i += 1
        return code

    def generate_python_code(self, tokens, substitutions={}):
        code = ""
        indent_level = 0

        # Helper function to manage indentation
        def add_indentation(level):
            return "    " * level  # 4 spaces per indent level

        i = 0
        while i < len(tokens):
            token_type, token_value = tokens[i]
            
            if token_type == "INT" or token_type == "FLOAT":
                # Skip type declarations in Python
                pass
            elif token_type == "SEMI":
                code += "\n" + add_indentation(indent_level)
            elif token_type == "LBRACE":
                indent_level += 1
                code += ":\n" + add_indentation(indent_level)
            elif token_type == "RBRACE":
                indent_level -= 1
                # Move the position back to align with the current block
                code = code.rstrip() + "\n" + add_indentation(indent_level)
            elif token_type == "INC_OP":
                # Increment operator, convert to Python style
                code += " += 1"
            elif token_type == "DEC_OP":
                # Decrement operator, convert to Python style
                code += " -= 1"
            elif token_type == "ID" and tokens[i + 1][0] == "ASSIGN":
                # print(f"ID {token_value} i {i} tokens[i] {tokens[i]}")
                # Variable assignment, check for a declaration and skip
                if (i > 0 and (tokens[i - 1][0] == "INT" or tokens[i - 1][0] == "FLOAT")):
                    # Output the variable name and assignment
                    code += token_value + " = "
                    i += 1  # Skip the assignment operator
                else:
                    # Output the variable name
                    code += token_value
            elif token_type in "WHILE":
                code += "while "  # Add a space after while
            elif token_type == "RETURN":
                code += "return "
            else:
                # Apply substitutions to IDs
                token_value = str(token_value)
                for subs in substitutions:
                    token_value = token_value.replace(subs, substitutions[subs])
                # Append other tokens directly
                code += token_value
            i += 1
        return code.strip()
    
    def generate_typescript_code(self, tokens, substitutions={}):
        code = ""
        indent_level = 0

        # Helper function to manage indentation
        def add_indentation(level):
            return "    " * level
        
        i = 0
        while i < len(tokens):
            token_type, token_value = tokens[i]
            if token_type == "LBRACE":
                # Add opening brace and increase indentation level
                code += " {\n"
                indent_level += 1
                code += add_indentation(indent_level)
            elif token_type == "RBRACE":
                # Decrease indentation level and add closing brace
                if indent_level > 0:
                    indent_level -= 1
                code += add_indentation(indent_level) + "}\n"
                if (i + 1) < len(tokens) and tokens[i + 1][0] != "RBRACE":
                    code += add_indentation(indent_level)
            elif token_type == "SEMI":
                # Add semicolon
                code += ";\n"
                if (i + 1) < len(tokens) and tokens[i + 1][0] != "RBRACE":
                    code += add_indentation(indent_level)
            elif token_type in ["WHILE", "RETURN"]:
                # Ensure keywords start on a new line with a space following them
                code += token_value + " "
            else:
                # Apply substitutions to IDs
                token_value = str(token_value)
                for subs in substitutions:
                    # Use regex to replace the ID with the value
                    # old_token_value = token_value
                    token_value = re.sub(subs, substitutions[subs], token_value)
                    # print(f"old_token_value {old_token_value} token_value {token_value} subs {subs} substitutions[subs] {substitutions[subs]}")
                # Append other tokens directly
                code += token_value
            i += 1
        return code

if __name__ == "__main__":

    # Create an instance of the PseudocodeHandler class
    pseudocode_handler = PseudocodeHandler()

    # Arguments
    parser = argparse.ArgumentParser(description="Convert pseudocode to C++ or Python code")
    # Optional arg to specify the input pseudocode
    parser.add_argument("--code", type=str, default="", help="Pseudocode to convert")
    # Optional arg to specify the output language
    parser.add_argument("--lang", type=str, default="cpp", help="Output language (cpp or python)")
    # Optional arg for verbose output
    parser.add_argument("--verbose", action="store_true", help="Enable verbose output")
    # Optional arg for substitutions
    parser.add_argument("--subs", type=str, default="", help="Substitutions to apply to IDs in the code - in the form 'ID1=VALUE1,ID2=VALUE2,...'")

    # Parse the arguments
    args = parser.parse_args()

    # Get the input pseudocode - if none specified then use the test code
    test_code = "int N=(buf[0]+32-buf[2])%32;int k=3;int i=0;while(i<N){out.Red=(buf[k]<<16)|(buf[k+1]<<8)|buf[k+2];out.IR=(buf[k+3]<<16)|(buf[k+4]<<8)|buf[k+5];k+=6;i++;next(out);}return i;"
    code = args.code if args.code else test_code

    # Tokenize the input pseudocode
    tokens = list(pseudocode_handler.lexer(code))

    # Print the tokens if verbose output is enabled
    if args.verbose:
        print(tokens)

    # Apply substitutions to the tokens if specified
    substitutions = {}
    if args.subs:
        for sub in args.subs.split(","):
            id, value = sub.split("=")
            substitutions[id] = value
    else:
        if args.lang == "cpp":
            loop_end_lines = [
                "if (++pOut >= pStruct + maxRecCount) break;",
                "pOut->timeMs = timestampUs / 1000;"
            ]
            # Add default substitutions for C++ code
            substitutions["^out."] = "pOut->",
            substitutions["next"] = "\n    ".join(loop_end_lines)
        elif args.lang == "typescript":
            loop_end_lines = [
                "if (++pOut >= pStruct + maxRecCount) break;",
            ]
            substitutions["int"] = "let "
            substitutions["float"] = "let "
            substitutions["out\.(.*)"] = "attrValues['\\1'].push(0); attrValues['\\1'][attrValues['\\1'].length-1] "
            substitutions["next"] = ""

    # Generate the output code based on the specified language
    if args.lang == "cpp":
        print(pseudocode_handler.generate_cpp_code(tokens, substitutions))
    elif args.lang == "python":
        print(pseudocode_handler.generate_python_code(tokens, substitutions))
    elif args.lang == "typescript":
        print(pseudocode_handler.generate_typescript_code(tokens, substitutions))
    else:
        print("Invalid language specified. Please use 'cpp' or 'python'")
        exit(1)


# Run the script with the following command:
# python scripts/PseudocodeHandler.py --code "int N=(buf[0]+32-buf[2])%32;int k=3;int i=0;while(i<N){out.Red=(buf[k]<<16)|(buf[k+1]<<8)|buf[k+2];out.IR=(buf[k+3]<<16)|(buf[k+4]<<8)|buf[k+5];k+=6;i++;next(out);}return i;" --lang cpp

        
