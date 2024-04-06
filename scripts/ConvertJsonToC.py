import json
import sys
import re

def json_to_header(json_path, header_path):
    with open(json_path, 'r') as json_file:
        dev_ident_json = json.load(json_file)

    with open(header_path, 'w') as header_file:
        header_file.write('#pragma once\n\n')
        dev_ident_lines = json.dumps(dev_ident_json, separators=(',', ':'),indent="    ").splitlines()
        for line in dev_ident_lines:
            indentGrp = re.search(r'^\s*', line)
            indentText = indentGrp.group(0) if indentGrp else ""
            header_file.write(f'    {indentText}R"({line.strip()})"\n')

if __name__ == "__main__":
    json_to_header(sys.argv[1], sys.argv[2])
    sys.exit(0)
