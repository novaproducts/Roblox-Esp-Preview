#pragma once
#include "obj_parser.hpp"
#include <vector>
#include <string>

bool parse_mtl(const std::vector<unsigned char>& mtl_data, c_obj_model& model, const std::vector<std::string>& texture_hashes);