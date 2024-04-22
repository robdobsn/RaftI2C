
import re
import os
from collections import defaultdict

def read_and_count(log_file_folder: str) -> dict:
    # This dictionary will hold the frequency count of each YY value
    counts = defaultdict(int)
    
    # File likes look like this: I (14843) BusScanner: getAddrAndGetSlotToScanNext slots addr 60 slot 26
    # This pattern will match the YY value in the line
    pattern = re.compile(r'addr ([0-9A-Fa-f]+) slot (\d+)')

    # Files in the logs folder are named like 20240421-173204.log
    # Find the latest file
    latest_file = max((f for f in os.listdir(log_file_folder) if f.endswith('.log')))
    file_path = os.path.join(log_file_folder, latest_file)

    print(f"Reading file: {file_path}")

    # Open the file and read line by line
    with open(file_path, 'r') as file:
        for line in file:
            match = pattern.search(line)
            if match:
                # Convert to uppercase if you want case-insensitive counting
                yy_value = match.group(1).upper()
                counts[yy_value] += 1
    
    return counts

# Example usage:
counts = read_and_count('./logs')
print(counts)

# Calculate factors for 1D vs 53 vs 04
total = sum(counts.values())
print(f"1D / 53: {counts['1D'] / counts['53']}")
print(f"1D / 04: {counts['1D'] / counts['04']}")

# Check all values between 0x04 and 0x7f are represented
for i in range(4, 0x80):
    if f"{i:02X}" not in counts:
        print(f"Value 0x{i:02X} not found")

