import json
from random import randint

# Base device type template
base_dev_type = {
    "ad": "0x60",
    "type": "VCNL4040",
    "fr": "VCNL4040",
    "man": "Vishay",
    "prt": "VCNL4040",
    "det": "0x0c=0b100001100000XXXX",
    "ini": "0x041007=&0x030e08=&0x000000=",
    "pol": {
        "dat": "0x08=0bXXXXXXXXXXXXXXXX&0x09=0bXXXXXXXXXXXXXXXX&0x0a=0bXXXXXXXXXXXXXXXX",
        "ms": 1000,
        "sto": 10
    }
}

def gen_random_addr_range():
    base_addr = randint(0x04, 0x6f)
    if randint(0, 1):
        return f"0x{base_addr:02x}"
    return f"0x{base_addr:02x}-0x{base_addr+randint(0,7):02x}"

# Function to generate a modified device type based on the base template
def generate_dev_type(index):
    dev_type = base_dev_type.copy()
    dev_type['ad'] = gen_random_addr_range()
    dev_type['type'] += str(index)
    dev_type['fr'] += str(index)
    dev_type['prt'] += str(index)
    # Modify other fields as needed to simulate variations
    dev_type['pol']['ms'] += index # Just an example of variation
    return dev_type

# Generate 100 entries
dev_types = {f"VCNL4040_{i}": generate_dev_type(i) for i in range(1, 101)}

# Overall JSON structure
test_data = {
    "devTypes": dev_types
}

# Write to a file
with open('test_data.json', 'w') as f:
    json.dump(test_data, f, indent=4)
