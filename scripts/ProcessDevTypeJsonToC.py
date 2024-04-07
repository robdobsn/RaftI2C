import json
import sys
import re

def process_dev_types(json_path, header_path):
    with open(json_path, 'r') as json_file:
        dev_ident_json = json.load(json_file)

    # Address indexes
    addr_index_to_dev_record = {}

    # Iterate records
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

        # Generate a dictionary with all addresses referring to a record index
        for addr in addr_list:
            if addr in addr_index_to_dev_record:
                addr_index_to_dev_record[addr].append(dev_record_index)
            else:
                addr_index_to_dev_record[addr] = [dev_record_index]

        # Bump index
        dev_record_index += 1

    # Generate array covering all addresses from 0x00 to 0x7f
    max_count_of_dev_types_for_addr = 0
    addr_index_to_dev_array = []
    for addr in range(0x00, 0x80):
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
        header_file.write('#pragma once\n\n')

        # Generate the BusI2CDevTypeRecord array
        header_file.write('static BusI2CDevTypeRecord baseDevTypeRecords[] =\n')
        header_file.write('{\n')

        # Iterate records
        dev_record_index = 0
        for dev_type in dev_ident_json['devTypes'].values():
            # Convert JSON parts to strings without unnecessary spaces
            polling_config_json_str = json.dumps(dev_type["pollingConfigJson"], separators=(',', ':'))
            dev_info_json_str = json.dumps(dev_type["devInfoJson"], separators=(',', ':'))
            header_file.write('    {\n')
            header_file.write(f'        R"({dev_type["deviceType"]})",\n')
            header_file.write(f'        R"({dev_type["addresses"]})",\n')
            header_file.write(f'        R"({dev_type["detectionValues"]})",\n')
            header_file.write(f'        R"({dev_type["initValues"]})",\n')
            header_file.write(f'        R"({polling_config_json_str})",\n')
            header_file.write(f'        R"({dev_info_json_str})"\n')
            header_file.write('    },\n')
            dev_record_index += 1

        header_file.write('};\n\n')

        # Write constants for the min and max values of array index
        header_file.write(f'static const uint32_t BASE_DEV_INDEX_BY_ARRAY_MIN_ADDR = 0;\n')
        header_file.write(f'static const uint32_t BASE_DEV_INDEX_BY_ARRAY_MAX_ADDR = 0x7f;\n\n')

        # Generate the count of device types for each address
        header_file.write(f'static const uint8_t baseDevTypeCountByAddr[] =\n')
        header_file.write('{\n    ')
        for addr_index in addr_index_to_dev_array:
            header_file.write(f'{len(addr_index)},')
        header_file.write('\n};\n\n')

        # For every non-zero length array, generate a C variable specific to that array
        for addr in range(0x00, 0x80):
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
        for addr in range(0x00, 0x80):
            if len(addr_index_to_dev_array[addr]) == 0:
                header_file.write('    nullptr,\n')
            else:
                header_file.write(f'    baseDevTypeIndexByAddr_0x{addr:02x},\n')

        header_file.write('};\n\n')

if __name__ == "__main__":
    process_dev_types(sys.argv[1], sys.argv[2])
    sys.exit(0)
