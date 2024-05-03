def get_polling_config_result_len_bytes(polling_config_record):
    
    # Parse the polling config record
    # Format is "0x08=0bXXXXXXXXXXXXXXXX&0x09=0bXXXXXXXXXXXXXXXX" or "0x08=r1&0x09=r2&0x0a=r9"
    pc_split = polling_config_record.split("&")
    pc_len = 0
    for pc in pc_split:
        # Split on =
        pc_eq = pc.split("=")
        pc_read_def = pc_eq[1].strip() if len(pc_eq) > 1 else ""
        # Check if the value is a binary string
        if pc_read_def.startswith("0b"):
            pc_bit_len = len(pc_read_def) - 2
            if pc_bit_len % 8 != 0:
                raise ValueError("Invalid polling config record value: " + pc_read_def)
            pc_len += pc_bit_len // 8
        # Check if the value is a read byte count
        elif pc_read_def.startswith("r"):
            pc_len += int(pc_read_def[1:])
        elif len(pc_read_def) == 0:
            pass
        else:
            raise ValueError("Invalid polling config record value: " + pc_read_def)
    return pc_len

def decode_generator_len_fn(dev_type_record):
    # Parse the device type record polling config record
    polling_config_record = dev_type_record["pollingConfigJson"]["c"]
    rslt_len = get_polling_config_result_len_bytes(polling_config_record)
    # Return a C++ anonymous function that returns the result length
    return "[]() -> uint32_t { return " + str(rslt_len) + "; }"

