import json
import sys
import re
import argparse

from DecodeGenerator import DecodeGenerator

# ProcessDevTypeJsonToC.py
# Rob Dobson 2024
# This script processes a JSON I2C device types file and generates a C header file
# with the device types and addresses. The header file contains the following:
# - An array of BusI2CDevTypeRecord structures - these contain the device type, addresses, detection values, init values, polling config and device info
# - An array of device type counts for each address (0x00 to 0x7f) - this is the number of device types for each address
# - An array of device type indexes for each address - each element is an array of indices into the BusI2CDevTypeRecord array
# - An array of scanning priorities for each address
# The script takes two arguments:
# - The path to the JSON file with the device types
# - The path to the header file to generate

def process_dev_types(json_path, header_path, gen_options):

    decodeGenerator = DecodeGenerator(gen_options)

    with open(json_path, 'r') as json_file:
        dev_ident_json = json.load(json_file)

    # Address range
    min_addr_array_index = 0
    max_addr_array_index = 0x7f
    min_valid_i2c_addr = 0x04
    max_valid_i2c_addr = 0x77

    # Address indexes
    addr_index_to_dev_record = {}

    # Scan priorities
    NUM_PRIORITY_LEVELS = 3
    addr_scan_priority_sets = [set() for _ in range(NUM_PRIORITY_LEVELS)]
    scan_priority_values = {
        "high": 0,
        "medium": 1,
        "low": 2
    }

    # Iterate device type records
    dev_record_index = 0
    for dev_type in dev_ident_json['devTypes'].values():
        # print(dev_type["addresses"])

        # Extract list of addresses from addresses field
        # Addresses field can be of the form 0xXX or 0xXX-0xYY or 0xXX,0xYY,0xZZ
        addr_list = []
        addresses = dev_type["addresses"]
        addr_range_list = addresses.split(",")
        for addr_range in addr_range_list:
            addr_range = addr_range.strip()
            if re.match(r'^0x[0-9a-fA-F]{2}-0x[0-9a-fA-F]{2}$', addr_range):
                addr_range_parts = addr_range.split("-")
                addr_start = int(addr_range_parts[0], 16)
                addr_end = int(addr_range_parts[1], 16)
                for addr in range(addr_start, addr_end+1):
                    addr_list.append(addr)
            elif re.match(r'^0x[0-9a-fA-F]{2}$', addr_range):
                addr_list.append(int(addr_range, 16))
            else:
                print(f"Invalid address range {addr_range}")
                sys.exit(1)

        # Debug
        addr_list_hex = ",".join([f'0x{addr:02x}' for addr in addr_list])
        print(f"Record {dev_record_index} Addresses {addr_range} Addr List {addr_list_hex}")

        # Scan priority
        if "scanPriority" in dev_type:
            scan_priority = dev_type["scanPriority"]

            # If scan_priority is an array then set scan priority for primary address to first
            # element and for all other addresses to the second element
            priority_value = scan_priority-1 if isinstance(scan_priority, int) else scan_priority_values[scan_priority] if isinstance(scan_priority, str) else NUM_PRIORITY_LEVELS-1
            if priority_value < 0:
                priority_value = 0
            if priority_value > NUM_PRIORITY_LEVELS-1:
                priority_value = NUM_PRIORITY_LEVELS-1
            if priority_value == 0:
                addr_scan_priority_sets[0].add(addr_list[0])
                for addr in addr_list[1:]:
                    addr_scan_priority_sets[1].add(addr)
            else:
                for addr in addr_list:
                    addr_scan_priority_sets[priority_value].add(addr)

        # Generate a dictionary with all addresses referring to a record index
        for addr in addr_list:
            if addr in addr_index_to_dev_record:
                addr_index_to_dev_record[addr].append(dev_record_index)
            else:
                addr_index_to_dev_record[addr] = [dev_record_index]

        # Bump index
        dev_record_index += 1

    # Generate array covering all addresses from 0x00 to 0x77
    max_count_of_dev_types_for_addr = 0
    addr_index_to_dev_array = []
    for addr in range(min_addr_array_index, max_addr_array_index+1):
        if addr in addr_index_to_dev_record:
            addr_index_to_dev_array.append(addr_index_to_dev_record[addr])
            if len(addr_index_to_dev_record[addr]) > max_count_of_dev_types_for_addr:
                max_count_of_dev_types_for_addr = len(addr_index_to_dev_record[addr])
        else:
            addr_index_to_dev_array.append([])

    # Debug
    print(addr_index_to_dev_record)

    # Generate header file
    with open(header_path, 'w') as header_file:

        # Write header
        header_file.write('#pragma once\n')
        header_file.write('#include <stdint.h>\n')
        header_file.write('using namespace Raft;\n\n')

        # Generate structs for the device type records if enabled
        if gen_options.get("gen_decode", False):
            struct_defs = decodeGenerator.get_struct_defs(dev_ident_json['devTypes'])
            for struct_def in struct_defs:
                header_file.write(struct_def)
                header_file.write("\n")

        # Generate the BusI2CDevTypeRecord array
        header_file.write('static BusI2CDevTypeRecord baseDevTypeRecords[] =\n')
        header_file.write('{\n')

        # Iterate records
        dev_record_index = 0
        for dev_type_key, dev_type in dev_ident_json['devTypes'].items():

            # Get the poll data size
            poll_data_size_bytes = decodeGenerator.poll_data_bytes(dev_type)

            # Check the size matches the JSON value
            poll_size_value_in_json = dev_type.get("devInfoJson", {}).get("resp", {}).get("b", 0)
            if poll_data_size_bytes != poll_size_value_in_json:
                print(f"Poll data size mismatch for {dev_type_key} JSON {poll_size_value_in_json} Calculated {poll_data_size_bytes}")
                sys.exit(1)

            # Convert JSON parts to strings without unnecessary spaces
            polling_config_json_str = json.dumps(dev_type["pollInfo"], separators=(',', ':'))
            dev_info_json_str = json.dumps(dev_type["devInfoJson"], separators=(',', ':'))
            header_file.write('    {\n')
            header_file.write(f'        R"({dev_type["deviceType"]})",\n')
            header_file.write(f'        R"({dev_type["addresses"]})",\n')
            header_file.write(f'        R"({dev_type["detectionValues"]})",\n')
            header_file.write(f'        R"({dev_type["initValues"]})",\n')
            header_file.write(f'        R"({polling_config_json_str})",\n')
            header_file.write(f'        {str(poll_data_size_bytes)},\n')
            if gen_options.get("inc_dev_info_json", False):
                header_file.write(f'        R"({dev_info_json_str})"')
            else:
                header_file.write(f'        nullptr')

            # Check if gen_decode is set
            if gen_options.get("gen_decode", False):
                header_file.write(f',\n        {decodeGenerator.decode_fn(dev_type)}')

            header_file.write('\n    },\n')
            dev_record_index += 1

        header_file.write('};\n\n')

        # Write constants for the min and max values of array index
        header_file.write(f'static const uint32_t BASE_DEV_INDEX_BY_ARRAY_MIN_ADDR = 0;\n')
        header_file.write(f'static const uint32_t BASE_DEV_INDEX_BY_ARRAY_MAX_ADDR = 0x77;\n\n')

        # Generate the count of device types for each address
        header_file.write(f'static const uint8_t baseDevTypeCountByAddr[] =\n')
        header_file.write('{\n    ')
        for addr_index in addr_index_to_dev_array:
            header_file.write(f'{len(addr_index)},')
        header_file.write('\n};\n\n')

        # For every non-zero length array, generate a C variable specific to that array
        for addr in range(min_addr_array_index, max_addr_array_index+1):
            if len(addr_index_to_dev_array[addr]) > 0:
                header_file.write(f'static uint16_t baseDevTypeIndexByAddr_0x{addr:02x}[] = ')
                header_file.write('{')
                if len(addr_index_to_dev_array[addr]) > 0:
                    header_file.write(f'{addr_index_to_dev_array[addr][0]}')
                    for i in range(1, len(addr_index_to_dev_array[addr])):
                        header_file.write(f', {addr_index_to_dev_array[addr][i]}')
                header_file.write('};\n')

        # Generate the BusI2CDevTypeRecord index by address
        header_file.write(f'\nstatic uint16_t* baseDevTypeIndexByAddr[] =\n')
        header_file.write('{\n')

        # Iterate address array
        for addr in range(min_addr_array_index, max_addr_array_index+1):
            if len(addr_index_to_dev_array[addr]) == 0:
                header_file.write('    nullptr,\n')
            else:
                header_file.write(f'    baseDevTypeIndexByAddr_0x{addr:02x},\n')

        header_file.write('};\n')

        # Generate the scan priority lists
        addr_scan_priority_lists = []
        for i in range(NUM_PRIORITY_LEVELS):
            if i == 0:
                addr_scan_priority_lists.append(sorted(addr_scan_priority_sets[i]))
            else:
                if i == NUM_PRIORITY_LEVELS-1:
                    addr_scan_priority_sets[i] = set(range(min_valid_i2c_addr, max_valid_i2c_addr+1))
                for j in range(i):
                    addr_scan_priority_sets[i] = addr_scan_priority_sets[i] - addr_scan_priority_sets[j]
                addr_scan_priority_lists.append(sorted(addr_scan_priority_sets[i]))

        # Write out the priority scan lists
        for i in range(NUM_PRIORITY_LEVELS):
            header_file.write(f'\n\nstatic const uint8_t scanPriority{i}[] =\n')
            header_file.write('{\n    ')
            for addr in addr_scan_priority_lists[i]:
                header_file.write(f'0x{addr:02x},')
            header_file.write('\n};\n')

        # Write a record for scan lists
        header_file.write('\nstatic const uint8_t* scanPriorityLists[] =\n')
        header_file.write('{\n')
        for i in range(NUM_PRIORITY_LEVELS):
            header_file.write(f'    scanPriority{i},\n')
        header_file.write('};\n')

        # Write out the lengths of the priority scan lists
        header_file.write('\nstatic const uint8_t scanPriorityListLengths[] =\n')
        header_file.write('{\n')
        for i in range(NUM_PRIORITY_LEVELS):
            header_file.write(f'    {len(addr_scan_priority_lists[i])},\n')
        header_file.write('};\n')

        # Write out the number of priority scan lists
        header_file.write(f'\nstatic const uint8_t numScanPriorityLists = {NUM_PRIORITY_LEVELS};\n')

if __name__ == "__main__":
    argparse = argparse.ArgumentParser()
    argparse.add_argument("json_path", help="Path to the JSON file with the device types")
    argparse.add_argument("header_path", help="Path to the header file to generate")
    # Arg for generation of c++ code to decode poll results
    argparse.add_argument("--gendecode", help="Generate C++ code to decode poll results", action="store_false")
    # Arg for inclusion of device info JSON in the header file
    argparse.add_argument("--incdevjson", help="Include device info JSON in the header file", action="store_false")
    # Arg for size of timestamp info in poll data
    argparse.add_argument("--pollresptsbytes", help="Poll response timestamp size (bytes)", type=int, default=2)
    # Arg for poll data timestamp resolution in us
    argparse.add_argument("--pollresptsresus", help="Poll response timestamp resolution in us", type=int, default=1000)
    # Arg for decoded timestamp C data type in extracted data struct
    argparse.add_argument("--decodestructtsctype", help="Decoded struct timestamp C data type", default="uint32_t") 
    # Arg for decoded timestamp C resolution in us
    argparse.add_argument("--decodestructtsresus", help="Decoded struct timestamp resolution in us", type=int, default=1000)
    # Arg for decoded timestamp variable name
    argparse.add_argument("--decodestructtsvar", help="Decoded struct timestamp variable name", default="")
    args = argparse.parse_args()
    gen_options = {
        "gen_decode": args.gendecode,
        "inc_dev_info_json": args.incdevjson,
        "POLL_RESULT_TIMESTAMP_SIZE": args.pollresptsbytes,
        "POLL_RESULT_RESOLUTION_US": args.pollresptsresus,
        "DECODE_STRUCT_TIMESTAMP_C_TYPE": args.decodestructtsctype,
        "DECODE_STRUCT_TIMESTAMP_RESOLUTION_US": args.decodestructtsresus,
        "struct_time_var_name": args.decodestructtsvar
    }
    process_dev_types(args.json_path, args.header_path, gen_options)
    sys.exit(0)
