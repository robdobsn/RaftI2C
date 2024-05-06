import re

# Pseudo-code input
pseudo_code = "let N=(d[0]+32-d[2])%32;let p=3;for i in 0..N-1{r[0].push((d[p]<<16)|(d[p+1]<<8|d[p+2]));r[1].push((d[p+3]<<16)|(d[p+4]<<8|d[p+5]));p+=6;}"

# Replace variable assignments
# typescript_code = re.sub(r'([a-zA-Z_][a-zA-Z0-9_]*)\s*=\s*(.*?);', r'let \1 = \2;\n', pseudo_code)

# Transform the range-based for-loop into a TypeScript for-loop
# Regex to match and convert a for-loop from pseudo-code to TypeScript
typescript_code = re.sub(
    r'for\s+([a-zA-Z_][a-zA-Z0-9_]*)\s+in\s+([a-zA-Z0-9_\(\)\+\-\*\/]*)\.\.([a-zA-Z0-9_\(\)\+\-\*\/]*)',
    r'for (let \1 = \2; \1 < \3; \1++)\n',
    pseudo_code
)

# Output the transformed TypeScript code
print(typescript_code)
