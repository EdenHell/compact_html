#include "base64.h"
#include "clipp.h"
#include <cpprest/http_client.h>
// #include <stdexcept>
#include <string>

#ifdef _WIN32
#include <AtlBase.h>
#endif
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

const std::regex img_src_pattern(R"#(<img src="(.*?)")#");

inline bool startswith(std::string_view s, std::string_view prefix) {
  return s.rfind(prefix, 0) == 0;
}

int main(int argc, char *argv[]) {
  std::string input_file;
  std::string output_file;
  auto cli = (clipp::value("input html file", input_file),
              clipp::opt_value("output html file", output_file),
              clipp::option("-h", "--help") % "show help");

  if (!parse(argc, argv, cli)) {
    std::cerr << "Usage:\n"
              << clipp::usage_lines(cli, argv[0]) << "\nOptions:\n"
              << clipp::documentation(cli) << std::endl;
    return 1;
  }
  std::filesystem::path src_html_path(input_file);
  if (!std::filesystem::is_regular_file(src_html_path) ||
      src_html_path.extension() != ".html") {
    std::cerr << "Usage:\n"
              << clipp::usage_lines(cli, argv[0]) << "\nOptions:\n"
              << clipp::documentation(cli) << std::endl;
    return 2;
  }
  std::ifstream html_file(src_html_path);
  if (!html_file.is_open()) {
    std::cerr << "Cannot read from input file!" << std::endl;
    return 3;
  }
  auto content = (std::stringstream() << html_file.rdbuf()).str();
  html_file.close();

  std::string new_content;

  std::smatch img_src_match;
  auto search_ret = regex_search(content, img_src_match, img_src_pattern);
  while (search_ret) {
    auto matched_str = img_src_match[0].str();
    auto captured_str = img_src_match[1].str();
    std::string img_type;
    std::string img_content;
    try {
      if (startswith(captured_str, "http")) {
        web::http::client::http_client client(web::uri::encode_uri(
            utility::conversions::to_string_t(captured_str)));
        auto rsp = client.request(web::http::methods::GET).get();
        if (rsp.status_code() != web::http::status_codes::OK) {
          throw std::runtime_error("Failed to request image data: " +
                                   captured_str);
        }
        auto _content_type = rsp.headers().content_type();
        auto content_type =
            std::string(_content_type.begin(), _content_type.end());
        if (content_type.empty()) {
          throw std::runtime_error("Failed to get Content-Type: " +
                                   captured_str);
        }
        std::string type_prefix = "image/";
        if (content_type.length() <= type_prefix.length() ||
            !startswith(content_type, type_prefix)) {
          throw std::runtime_error("Failed to identify image type: " +
                                   captured_str);
        }
        img_type = content_type.substr(type_prefix.length());
        auto char_vector = rsp.extract_vector().get();
        img_content = std::string(char_vector.begin(), char_vector.end());
      } else if (!startswith(captured_str, "data:image/")) {
#ifdef _WIN32
        std::filesystem::path img_file_path(
            std::wstring(CA2W(captured_str.c_str(), CP_UTF8))); // only windows
#else
        std::filesystem::path img_file_path(captured_str);
#endif
        auto img_file_ext = img_file_path.extension().string();
        if (img_file_ext.size() <= 1) {
          throw std::runtime_error(
              "The image file name must have an extension: " + captured_str);
        }
        img_type = img_file_ext.substr(1);
        std::ifstream img_file(img_file_path, std::ios::binary);
        if (!img_file.is_open()) {
          throw std::runtime_error("Could not open file: " + captured_str);
        }
        img_content = (std::stringstream() << img_file.rdbuf()).str();
        img_file.close();
      }
    } catch (std::runtime_error &e) {
      std::cout << e.what() << std::endl;
    } catch (...) {
      std::cout << "Failed to load image data: " << captured_str << std::endl;
    }
    new_content += img_src_match.prefix();
    content = img_src_match.suffix();
    if (!img_type.empty() && !img_content.empty()) {
      new_content += "<img src=\"data:image/" + img_type + ";base64," +
                     base64_encode(img_content) + "\"";
    } else {
      new_content += matched_str;
    }
    search_ret = regex_search(content, img_src_match, img_src_pattern);
    if (!search_ret) {
      new_content += content;
    }
  }
  std::filesystem::path dst_html_path;
  if (output_file.empty()) {
    dst_html_path =
        std::filesystem::path(input_file).replace_extension(".compact.html");
  } else {
    dst_html_path = std::filesystem::path(output_file);
  }
  std::ofstream embedded_html_file(dst_html_path);
  if (!embedded_html_file.is_open()) {
    std::cerr << "Cannot write to output file!" << std::endl;
    return 4;
  }
  embedded_html_file << (new_content.empty() ? content : new_content);
  embedded_html_file.close();
  return 0;
}
