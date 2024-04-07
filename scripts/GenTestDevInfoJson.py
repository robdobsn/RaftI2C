import json
from random import randint

# Base device type template
base_dev_type = {
    "addressRange": "0x60",
    "deviceType": "VCNL4040",
    "detectionValues": "0x0c=0b100001100000XXXX",
    "initValues": "0x041007=&0x030e08=&0x000000=",
    "pollingConfigJson":
    {
        "c": "0x08=0bXXXXXXXXXXXXXXXX&0x09=0bXXXXXXXXXXXXXXXX&0x0a=0bXXXXXXXXXXXXXXXX",
        "i": 1000,
        "s": 10
    },
    "devInfoJson":
    {
        "n": "VCNL4040",
        "d": "Prox&ALS",
        "m": "Vishay",
        "t": "VCNL4040",
        "p":
        [
            {
                "n": "prox",
                "t": "uint16",
                "u": "mm",
                "r": [0, 2000],
                "d": 1
            },
            {
                "n": "als",
                "t": "uint16",
                "u": "lux",
                "r": [0, 10000],
                "d": 1
            }
        ]
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
    dev_type['addressRange'] = gen_random_addr_range()
    dev_type['deviceType'] += str(index)
    return dev_type

def generate_dev_types_file(out_file):

    # Generate 100 entries
    dev_types = {f"VCNL4040_{i}": generate_dev_type(i) for i in range(1, 101)}

    # Overall JSON structure
    test_data = {
        "devTypes": dev_types
    }

    # Write to a file
    with open(out_file, 'w') as f:
        json.dump(test_data, f, indent=4)

    # Print the generated JSON
    print("Generated JSON in file ", out_file)

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description='Generate test device type JSON file')
    parser.add_argument('output_file', type=str, help='Output file path')
    args = parser.parse_args()
    generate_dev_types_file(args.output_file)
