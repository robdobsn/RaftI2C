import re

class DecodeGenerator:

    def __init__(self):
        self.type_map = {
            'b': ['int8_t', 'getUint8AndInc'],          # Signed byte
            'B': ['uint8_t', 'getInt8AndInc'],          # Unsigned byte
            '>h': ['int16_t', 'getBEInt16AndInc'],      # Big-endian signed short
            '<h': ['int16_t', 'getLEInt16AndInc'],      # Little-endian signed short
            '>H': ['uint16_t', 'getBEUint16AndInc'],    # Big-endian unsigned short
            '<H': ['uint16_t', 'getLEUint16AndInc'],    # Little-endian unsigned short
            '>i': ['int32_t', 'getBEInt32AndInc'],  
            '<i': ['int32_t', 'getLEInt32AndInc'],  
            '>I': ['uint32_t', 'getBEUint32AndInc'],
            '<I': ['uint32_t', 'getLEUint32AndInc'],
            '>l': ['int32_t', 'getBEInt32AndInc'],  
            '<l': ['int32_t', 'getLEInt32AndInc'],  
            '>L': ['uint32_t', 'getBEUint32AndInc'],
            '<L': ['uint32_t', 'getLEUint32AndInc'],
            '>q': ['int64_t', 'getBEInt64AndInc'],  
            '<q': ['int64_t', 'getLEInt64AndInc'],
            '>Q': ['uint64_t', 'getBEUint64AndInc'],
            '<Q': ['uint64_t', 'getLEUint64AndInc'],
            '>f': ['float', 'getBEfloat32AndInc'], 
            '<f': ['float', 'getLEfloat32AndInc'], 
            '>d': ['double', 'getBEdouble64AndInc'],
            '<d': ['double', 'getLEdouble64AndInc'],
        }
    
    def _get_polling_config_result_len_bytes(self, polling_config_record):
        
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
    
    def get_c_eqv_type(self, attr_type):
        # Attr types are strings used by the python struct module
        # Convert to C equivalent type
        # https://docs.python.org/3/library/struct.html#format-characters
        # https://en.cppreference.com/w/c/language/arithmetic_types

        if len(attr_type) == 0:
            return ""
        
        # Get C type from map
        c_type = self.type_map.get(attr_type, None)
        if c_type is None:
            print("get_c_eqv_type unknown type: " + attr_type)
            return ""
        return c_type[0]

    def to_valid_c_var_name(self, attr_name):
        # Replace any sequence of non-word characters (including spaces) with an underscore
        attr_name = re.sub(r'\W+', '_', attr_name)
        
        # Ensure the variable name does not start with a digit
        if attr_name and attr_name[0].isdigit():
            attr_name = '_' + attr_name
        return attr_name

    def form_attr_def(self, attr_rec) -> str:
        # Extract attribute name
        attr_name = attr_rec.get("n", "")
        if attr_name == "":
            return ""
        
        # Ensure name is a valid C variable name
        attr_name = self.to_valid_c_var_name(attr_name)

        # Get type
        attr_type = attr_rec.get("t", "")
        if attr_type == "":
            return ""
        
        # Get C type
        c_type = self.get_c_eqv_type(attr_type)

        # C field
        return c_type + " " + attr_name
        
    def gen_struct_name(self, dev_info_json):
        # Get the device type record name
        dev_type_name = dev_info_json.get("name", "")
        if dev_type_name == "":
            return ""
        return "struct poll_" + self.to_valid_c_var_name(dev_type_name)
    
    def gen_struct_elements(self, dev_info_json):
        # Iterate through attributes in dev_info_json["resp"] and generate struct elements for each
        poll_response = dev_info_json.get("resp", {})
        poll_response_attrs = poll_response.get("a", [])
        el_list = []
        for el in poll_response_attrs:
            attr_def = self.form_attr_def(el)
            if attr_def == "":
                continue
            el_list.append("    " + attr_def + ";\n")
        return "".join(el_list)

    def gen_extract_code(self, dev_info_json, line_prefix):
        poll_response = dev_info_json.get("resp", {})
        poll_response_attrs = poll_response.get("a", [])
        line_prefix = line_prefix + "    "
        extract_code = []
        if len(poll_response_attrs) == 0:
            extract_code.append(f"{line_prefix}return 0;\n")
            return "".join(extract_code)
        # Struct
        struct_name = self.gen_struct_name(dev_info_json)
        extract_code.append(f"{line_prefix}{struct_name}* pStruct = ({struct_name}*) pStructOut;\n")
        extract_code.append(f"{line_prefix}const uint8_t* pBufEnd = pBuf + bufLen;\n")
        extract_code.append(f"{line_prefix}uint32_t numRecs = 0;\n")
        extract_code.append(f"{line_prefix}for (int i = 0; i < maxRecCount; i++){{\n")
        # Iterate through attributes in devInfoJson["resp"] and generate extraction code for each
        for el in poll_response_attrs:
            attr_name = el.get("n", "")
            if attr_name == "":
                continue
            attr_name = self.to_valid_c_var_name(attr_name)
            attr_type = el.get("t", "")
            if attr_type == "":
                continue
            c_type = self.get_c_eqv_type(attr_type)
            if c_type == "":
                continue
            attr_get_and_inc_fn = self.type_map.get(attr_type, None)[1]
            extract_code.append(f"{line_prefix}    if (pBuf + sizeof(" + c_type + ") > pBufEnd) break;\n")
            extract_code.append(f"{line_prefix}    pStruct->{attr_name} = {attr_get_and_inc_fn}(pBuf, pBufEnd);\n")
        extract_code.append(f"{line_prefix}    numRecs++;\n")
        extract_code.append(f"{line_prefix}}}\n")
        extract_code.append(f"{line_prefix}return numRecs;\n")
        return "".join(extract_code)
    
    def poll_data_bytes(self, dev_type_record):
        # Parse the device type record polling groups record
        poll_info = dev_type_record.get("pollInfo", {})
        poll_config_record = poll_info.get("c", "")
        poll_rslt_len = self._get_polling_config_result_len_bytes(poll_config_record)
        return poll_rslt_len

    def decode_fn(self, dev_type_record):
        dev_info_json = dev_type_record.get("devInfoJson", {})
        struct_name = self.gen_struct_name(dev_info_json)
        if struct_name == "":
            return "nullptr"
        fn_def =  "[](const uint8_t* pBuf, uint32_t bufLen, void* pStructOut, uint32_t structOutSize, uint16_t maxRecCount) -> uint32_t {\n"
        fn_def += self.gen_extract_code(dev_info_json, "        ")
        fn_def += "        }"
        return fn_def

    def get_struct_defs(self, dev_type_records):
        struct_defs = []
        for dev_type_record in dev_type_records.values():
            dev_info_json = dev_type_record.get("devInfoJson", {})
            struct_name = self.gen_struct_name(dev_info_json)
            if struct_name == "":
                return "nullptr"
            struct_def = struct_name + " {\n"
            struct_def += self.gen_struct_elements(dev_info_json)
            struct_def += "};\n"
            struct_defs.append(struct_def)

        return struct_defs

