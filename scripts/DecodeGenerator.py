import re

from PseudocodeHandler import PseudocodeHandler

class DecodeGenerator:

    def __init__(self, generator_options):

        # Params
        self.POLL_RESULT_TIMESTAMP_SIZE = generator_options.get("POLL_RESULT_TIMESTAMP_SIZE", 2)
        self.POLL_RESULT_WRAP_VALUE = 0x10000 if self.POLL_RESULT_TIMESTAMP_SIZE == 2 else 0x100000000
        self.POLL_RESULT_RESOLUTION_US = generator_options.get("POLL_RESULT_RESOLUTION_US", 1000)

        # Time stamp size in decoded records
        self.DECODE_STRUCT_TIMESTAMP_C_TYPE = generator_options.get("DECODE_STRUCT_TIMESTAMP_C_TYPE", "uint32_t")
        self.DECODE_STRUCT_TIMESTAMP_RESOLUTION_US = generator_options.get("DECODE_STRUCT_TIMESTAMP_RESOLUTION_US", 1000)
        ts_var_name_lookup = {
            1: "timeUs",
            10: "time10Us",
            100: "time100Us",
            1000: "timeMs",
        }
        self.struct_time_var_name = generator_options.get("struct_time_var_name", "")
        if self.struct_time_var_name == "":
            self.struct_time_var_name = ts_var_name_lookup.get(self.DECODE_STRUCT_TIMESTAMP_RESOLUTION_US, "timestamp")

        # Decoding maps
        self.pystruct_map = {
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
        self.c_like_types = {
            'int8': 'int8_t',
            'int8_t': 'int8_t',
            'uint8': 'uint8_t',
            'uint8_t': 'uint8_t',
            'int16': 'int16_t',
            'int16_t': 'int16_t',
            'uint16': 'uint16_t',
            'uint16_t': 'uint16_t',
            'int32': 'int32_t',
            'int32_t': 'int32_t',   
            'uint32': 'uint32_t',
            'uint32_t': 'uint32_t',
            'int64': 'int64_t',
            'int64_t': 'int64_t',
            'uint64': 'uint64_t',
            'uint64_t': 'uint64_t',
            'float': 'float',
            'double': 'double',
            'bool': 'bool',
        }
        self.larger_signed_c_type = {
            'uint8_t': 'int16_t',
            'int8_t': 'int16_t',
            'uint16_t': 'int32_t',
            'int16_t': 'int32_t',
            'uint32_t': 'int64_t',
            'int32_t': 'int64_t',
            'uint64_t': 'int64_t',
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
    
    def _get_larger_type(self, c_type, larger_signed_type):
        if larger_signed_type and c_type in self.larger_signed_c_type:
            return self.larger_signed_c_type[c_type]
        return c_type

    def get_c_eqv_type(self, attr_rec, use_o_field_if_present, larger_signed_type):
        
        # Check if there is a specific definition in the "o" field
        if use_o_field_if_present:
            c_attr_base = attr_rec.get("o", "")
            if c_attr_base in self.c_like_types:
                return self._get_larger_type(self.c_like_types[c_attr_base], larger_signed_type)
            
        # Attr types are strings used by the python struct module
        # Convert to C equivalent type
        # https://docs.python.org/3/library/struct.html#format-characters
        # https://en.cppreference.com/w/c/language/arithmetic_types
        pystruct_type = attr_rec.get("t", "")
        if len(pystruct_type) == 0:
            return ""
        
        # Get C type from pystruct map
        pystruct_rec = self.pystruct_map.get(pystruct_type, None)
        if pystruct_rec is None:
            print("get_c_eqv_type unknown type: " + pystruct_type)
            return ""
        return self._get_larger_type(pystruct_rec[0], larger_signed_type)

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
        pystruct_type = attr_rec.get("t", "")
        if pystruct_type == "":
            return ""
        
        # Get C type
        c_type = self.get_c_eqv_type(attr_rec, True, False)

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
        poll_resp_meta = dev_info_json.get("resp", {})
        poll_resp_meta_attrs = poll_resp_meta.get("a", [])
        el_list = []
        el_list.append("    " + self.DECODE_STRUCT_TIMESTAMP_C_TYPE + " " + self.struct_time_var_name + ";\n")
        for el in poll_resp_meta_attrs:
            attr_def = self.form_attr_def(el)
            if attr_def == "":
                continue
            el_list.append("    " + attr_def + ";\n")
        return "".join(el_list)
    
    def is_attr_type_signed(self, attrType: str) -> bool:
        if len(attrType) == 0:
            return False
        attrStr = attrType[1] if attrType[0] == ">" or attrType[0] == "<" and len(attrType) > 1 else attrType[0]
        return attrStr == "b" or attrStr == "h" or attrStr == "i" or attrStr == "l" or attrStr == "q"

    def gen_timestamp_extract_code(self, extract_code, line_prefix):
        # Generate code to extract timestamp
        extract_code.append("\n" + line_prefix + "// Extract timestamp\n")
        if self.POLL_RESULT_TIMESTAMP_SIZE == 2:
            extract_code.append(line_prefix + "uint64_t timestampUs = getBEUint16AndInc(pBuf, pBufEnd) * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;\n")
        else:
            extract_code.append(line_prefix + "uint64_t timestampUs = getBEUint32AndInc(pBuf, pBufEnd) * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;\n")
        extract_code.append(line_prefix + "if (timestampUs < decodeState.lastReportTimestampUs) {\n")
        extract_code.append(line_prefix + "    decodeState.reportTimestampOffsetUs += DevicePollingInfo::POLL_RESULT_WRAP_VALUE * DevicePollingInfo::POLL_RESULT_RESOLUTION_US;\n")
        extract_code.append(line_prefix + "}\n")
        extract_code.append(line_prefix + "decodeState.lastReportTimestampUs = timestampUs;\n")
        extract_code.append(line_prefix + "timestampUs += decodeState.reportTimestampOffsetUs;\n")
        extract_code.append(line_prefix + "pOut->" + self.struct_time_var_name + f" = timestampUs / {self.DECODE_STRUCT_TIMESTAMP_RESOLUTION_US};\n")
    
    def gen_custom_extraction_code(self, poll_resp_meta, extract_code, line_prefix):

        # Create a pseudocode handler
        pseudocodeHandler = PseudocodeHandler()

        # Get custom function metadata
        custom_fn_metadata = poll_resp_meta.get("c", {})

        # Get the custom function (written in pseudocode)
        custom_pseudo_code = custom_fn_metadata.get("c", "")
        if custom_pseudo_code == "":
            return
        
        # Get the record time increment "us" field
        record_time_increment_us = poll_resp_meta.get("us", 0)
        loop_end_lines = [
            "if (++pOut >= pStruct + maxRecCount) break;",
            "timestampUs += " + str(record_time_increment_us) + ";",
            f"pOut->{self.struct_time_var_name} = timestampUs / {self.DECODE_STRUCT_TIMESTAMP_RESOLUTION_US};"
        ]
                
        # Generate the C++ code
        cpp_code = pseudocodeHandler.generate_cpp_code(list(pseudocodeHandler.lexer(custom_pseudo_code)), 
                                {
                                    "^out.": "pOut->", 
                                    "next": "\n    ".join(loop_end_lines)
                                })

        # Get the length of each record in bytes
        poll_resp_len_inc_timestamp = poll_resp_meta["b"] + self.POLL_RESULT_TIMESTAMP_SIZE

        # Iterate over records
        extract_code.append(line_prefix + "uint32_t pollRecIdx = 0;\n")
        extract_code.append(line_prefix + "while (pOut < pStruct + maxRecCount) {\n\n")
        extract_code.append(line_prefix + "    // Calculate record start and check size\n")
        extract_code.append(line_prefix + f"    const uint8_t* pBuf = pBufIn + {poll_resp_len_inc_timestamp} * pollRecIdx;\n")
        extract_code.append(line_prefix + f"    if (pBuf + {poll_resp_len_inc_timestamp} > pBufEnd) break;\n")

        # Generate code to extract timestamp
        self.gen_timestamp_extract_code(extract_code, line_prefix + "    ")

        # Comment
        extract_code.append("\n" + line_prefix + "    // Custom function\n")

        # Add the custom code
        extract_code.append(f"{line_prefix}    const uint8_t* buf = pBuf;\n")
        extract_code.append("\n".join([f"{line_prefix}    {line}" for line in cpp_code.split("\n")]))

        # Complete the loop
        extract_code.append("\n" + line_prefix + "    // Complete loop\n")
        extract_code.append(f"{line_prefix}    pollRecIdx++;\n")
        extract_code.append(line_prefix + "}\n")

    def gen_attr_extraction_code(self, poll_resp_meta, extract_code, line_prefix):

        # Get the length of each record in bytes
        poll_resp_len_inc_timestamp = poll_resp_meta["b"] + self.POLL_RESULT_TIMESTAMP_SIZE

        # Iterate over records
        extract_code.append(f"{line_prefix}uint32_t numRecs = 0;\n")
        extract_code.append(line_prefix + "while (numRecs < maxRecCount) {\n\n")
        extract_code.append(line_prefix + "    // Calculate record start\n")
        extract_code.append(line_prefix + f"    const uint8_t* pBuf = pBufIn + {poll_resp_len_inc_timestamp} * numRecs;\n")
        extract_code.append(line_prefix + f"    if (pBuf + {poll_resp_len_inc_timestamp} > pBufEnd) break;\n")

        # Generate code to extract timestamp
        self.gen_timestamp_extract_code(extract_code, line_prefix + "    ")

        # Check if absolute position is used in any attribute
        if any("at" in el for el in poll_resp_meta.get("a", [])):
            extract_code.append(line_prefix + "    const uint8_t* pAttrStart = pBuf;\n")

        # Comment
        extract_code.append("\n" + line_prefix + "    // Extract attributes\n")
        
        # Iterate through attributes in devInfoJson["resp"] and generate extraction code for each
        for el in poll_resp_meta.get("a", []):

            # Get final attribute name
            attr_name = el.get("n", "")
            if attr_name == "":
                continue
            attr_name = self.to_valid_c_var_name(attr_name)
            
            # Type of element in the buffer (codes from python struct module)
            pystruct_type = el.get("t", "")
            if pystruct_type == "":
                continue
            buf_elem_type = self.get_c_eqv_type(el, False, False)
            if buf_elem_type == "":
                continue

            # Type for intermediate variable (which is needed because masking and other operations may be needed)
            intermediate_type = self.get_c_eqv_type(el, False, True)

            # Generate a block to stop C++ from complaining about reused variable names
            extract_code.append(f"{line_prefix}    " + "{\n")

            # Check if the "at" field is present which means absolute position in the buffer
            attr_pos = el.get("at", "")
            var_pPos = "pBuf"
            if attr_pos != "":
                # Create pAbsPos for the absolute position of the element
                extract_code.append(f"{line_prefix}        const uint8_t* pAbsPos = pAttrStart + {attr_pos};\n")
                var_pPos = "pAbsPos"

            # Generate code to check the buffer bounds
            extract_code.append(f"{line_prefix}        if ({var_pPos} + sizeof({buf_elem_type}) > pBufEnd) break;\n")

            # Mask on signed value handling
            pystruct_type_for_extract = pystruct_type
            if self.is_attr_type_signed(pystruct_type) and "m" in el:
                # Upper case the type to get the unsigned version
                pystruct_type_for_extract = pystruct_type.upper()

            # Get the function to extract the attribute from the buffer
            attr_get_and_inc_fn = self.pystruct_map.get(pystruct_type_for_extract, None)[1]

            # Generate code to extract the attribute from the buffer
            extract_code.append(f"{line_prefix}        {intermediate_type} __{attr_name} = {attr_get_and_inc_fn}({var_pPos}, pBufEnd);\n")

            # Check for XOR mask
            if "x" in el:
                mask = int(el["x"], 0)
                extract_code.append(f"{line_prefix}        __{attr_name} ^= 0x{mask:x};\n")
                
            # Generate AND mask code
            if "m" in el:
                mask = int(el["m"], 0)
                if self.is_attr_type_signed(pystruct_type):
                    sign_bit_mask = (mask + 1) >> 1
                    extract_code.append(f"{line_prefix}        if (__{attr_name} & {sign_bit_mask})" + " {\n")
                    extract_code.append(f"{line_prefix}            __{attr_name} |= ~{mask} & ~{sign_bit_mask};\n")
                    extract_code.append(f"{line_prefix}        " + "}\n")
                else:
                    extract_code.append(f"{line_prefix}        __{attr_name} &= 0x{mask:x};\n")

            # Check for sign-bit and subtract handling
            if "sb" in el:
                sign_bit_pos = int(el["sb"])
                sign_bit_mask = 1 << sign_bit_pos
                if "ss" in el:
                    sign_bit_subtract = int(el["ss"])
                    extract_code.append(f"{line_prefix}        if (__{attr_name} & 0x{sign_bit_mask:x}) __{attr_name} = 0x{sign_bit_subtract:x} - __{attr_name};\n")
                else:
                    extract_code.append(f"{line_prefix}        if (__{attr_name} & 0x{sign_bit_mask:x}) __{attr_name} -= {sign_bit_mask << 1};\n")

            # Check for bit shift required
            if "s" in el and el["s"] != 0:
                bitshift = el["s"]
                if bitshift > 0:
                    extract_code.append(f"{line_prefix}        __{attr_name} <<= {bitshift};\n")
                elif bitshift < 0:
                    extract_code.append(f"{line_prefix}        __{attr_name} >>= {-bitshift};\n")

            # Generate code to store the value to the struct
            extract_code.append(f"{line_prefix}        pOut->{attr_name} = __{attr_name};\n")
            
            # Check for divisor
            if "d" in el and el["d"] != 0:
                divisor = el["d"]
                extract_code.append(f"{line_prefix}        pOut->{attr_name} /= {divisor};\n")

            # Check for addition
            if "a" in el and el["a"] != 0:
                addition = el["a"]
                extract_code.append(f"{line_prefix}        pOut->{attr_name} += {addition};\n")
            
            # End block to stop C++ from complaining about reused variables
            extract_code.append(f"{line_prefix}    " + "}\n")

        # Complete the loop
        extract_code.append("\n" + line_prefix + "    // Complete loop\n")
        extract_code.append(f"{line_prefix}    pOut++;\n")
        extract_code.append(f"{line_prefix}    numRecs++;\n")
        extract_code.append(line_prefix + "}\n")

    def gen_extract_code(self, dev_info_json, line_prefix):
        # Get the response meta data
        poll_resp_meta = dev_info_json.get("resp", {})
        line_prefix = line_prefix + "    "
        extract_code = []

        # Check if there are any attributes to extract
        poll_resp_meta_attrs = poll_resp_meta.get("a", [])
        if len(poll_resp_meta_attrs) == 0:
            extract_code.append(f"{line_prefix}return 0;\n")
            return "".join(extract_code)
        
        # Check there is a poll resp size
        if "b" not in poll_resp_meta:
            extract_code.append(f"{line_prefix}return 0;\n")
            return "".join(extract_code)
                
        # Output struct(s)
        struct_name = self.gen_struct_name(dev_info_json)
        extract_code.append(f"{line_prefix}{struct_name}* pStruct = ({struct_name}*) pStructOut;\n")
        extract_code.append(f"{line_prefix}{struct_name}* pOut = pStruct;\n")
        extract_code.append(f"{line_prefix}const uint8_t* pBufEnd = pBufIn + bufLen;\n")

        # Comment
        extract_code.append("\n" + line_prefix + "// Iterate through records\n")

        # Handle custom or regular decode
        if "c" in poll_resp_meta:
            self.gen_custom_extraction_code(poll_resp_meta, extract_code, line_prefix)
        else:
            self.gen_attr_extraction_code(poll_resp_meta, extract_code, line_prefix)

        # Return the number of records extracted
        extract_code.append(f"{line_prefix}return pOut - pStruct;\n")
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
        fn_def =  "[](const uint8_t* pBufIn, uint32_t bufLen, void* pStructOut, uint32_t structOutSize, \n\
                        uint16_t maxRecCount, RaftBusDeviceDecodeState& decodeState) -> uint32_t {\n"
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

