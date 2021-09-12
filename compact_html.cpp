#include "base64.h"
#include <cpprest/http_client.h>
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
  if (argc < 2) {
    std::cout << "Usage: " << argv[0] << " HTML_FILE_NAME" << std::endl;
    return 1;
  }
  std::filesystem::path src_html_path(argv[1]);
  if (!std::filesystem::is_regular_file(src_html_path) ||
      src_html_path.extension() != ".html") {
    return 2;
  }
  std::ifstream html_file(src_html_path);
  if (!html_file.is_open()) {
    std::cout << "Oooops" << std::endl;
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
          throw std::runtime_error("Oooops");
        }
        auto _content_type = rsp.headers().content_type();
        auto content_type =
            std::string(_content_type.begin(), _content_type.end());
        if (content_type.empty()) {
          throw std::runtime_error("Oooops");
        }
        std::string type_prefix = "image/";
        if (content_type.length() <= type_prefix.length() ||
            !startswith(content_type, type_prefix)) {
          throw std::runtime_error("Oooops");
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
        if (!img_file_path.is_absolute()) {
          img_file_path = src_html_path.parent_path() / img_file_path;
        }
        auto img_file_ext = img_file_path.extension().string();
        if ((img_file_ext.size() <= 1) ||
            !std::filesystem::is_regular_file(img_file_path)) {
          throw std::runtime_error("Oooops");
        }
        img_type = img_file_ext.substr(1);
        std::ifstream img_file(img_file_path, std::ios::binary);
        if (!img_file.is_open()) {
          std::cout << "Oooops" << std::endl;
          throw std::runtime_error("Oooops");
        }
        img_content = (std::stringstream() << img_file.rdbuf()).str();
        img_file.close();
      }
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
  auto dst_html_path =
      std::filesystem::path(src_html_path).replace_extension(".compact.html");
  if (!new_content.empty()) {
    std::ofstream embedded_html_file(dst_html_path);
    embedded_html_file << new_content;
    embedded_html_file.close();
  } else {
    std::filesystem::copy_file(
        src_html_path, dst_html_path,
        std::filesystem::copy_options::overwrite_existing);
  }
  return 0;
}
