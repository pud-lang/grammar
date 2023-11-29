#ifndef PEG_H
#define PEG_H

#include <algorithm>
#include <any>
#include <cassert>
#include <cctype>
#if __has_include(<charconv>)
#include <charconv>
#endif
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if !defined(__cplusplus) || __cplusplus < 201703L
#error "Requires complete C++17 support"
#endif

namespace peg {

/*-----------------------------------------------------------------------------
 *  scope_exit
 *---------------------------------------------------------------------------*/

// This is based on
// "http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2014/n4189".

template <typename EF>
struct scope_exit {
  explicit scope_exit(EF&& f)
      : exit_function(std::move(f)), execute_on_destruction{true} {}

  scope_exit(scope_exit&& rhs)
      : exit_function(std::move(rhs.exit_function)),
        execute_on_destruction{rhs.execute_on_destruction} {
    rhs.release();
  }

  ~scope_exit() {
    if (execute_on_destruction) {
      this->exit_function();
    }
  }

  void release() { this->execute_on_destruction = false; }

 private:
  scope_exit(const scope_exit&) = delete;
  void operator=(const scope_exit&) = delete;
  scope_exit& operator=(scope_exit&&) = delete;

  EF exit_function;
  bool execute_on_destruction;
};

/*-----------------------------------------------------------------------------
 *  UTF8 functions
 *---------------------------------------------------------------------------*/

/// 以下函数用于处理 UTF-8 编码的字符串。

// UTF-8 编码规格
// UTF-8 使用不同数量的字节来表示不同范围的 Unicode 码点：
// 
// 1字节: 用于表示基本的 ASCII 字符（U+0000 至 U+007F）。
//        这些字符的首位是0，后面是字符的ASCII码。
// 2字节: 用于表示 U+0080 至 U+07FF 范围的字符。
//        第一个字节以二进制的 110 开头，第二个字节以 10 开头。
// 3字节: 用于表示 U+0800 至 U+FFFF 范围的字符（包括基本多文种平面上的大多数字符）。
//        第一个字节以 1110 开头，随后两个字节都以 10 开头。
// 4字节: 用于表示 U+10000 至 U+10FFFF 范围的字符（包括其他辅助平面的字符）。
//        第一个字节以 11110 开头，随后三个字节都以 10 开头。

// 计算给定 UTF-8 编码字符串中第一个码点的字节长度。
inline size_t codepoint_length(const char* s8, size_t l) {
  if (l) {
    auto b = static_cast<uint8_t>(s8[0]);
    // 首先检查长度是否为0，如果是，则返回0。
    if ((b & 0x80) == 0) {
      return 1;
      // 然后根据UTF-8编码规则，检查首字节的位模式来确定当前码点的长度。
      // 返回码点占用的字节数（1到4字节之间）。
    } else if ((b & 0xE0) == 0xC0 && l >= 2) {
      return 2;
    } else if ((b & 0xF0) == 0xE0 && l >= 3) {
      return 3;
    } else if ((b & 0xF8) == 0xF0 && l >= 4) {
      return 4;
    }
  }
  return 0;
}

// 计算 UTF-8 编码字符串中的码点数量。
inline size_t codepoint_count(const char* s8, size_t l) {
  size_t count = 0;
  // 遍历字符串，使用 codepoint_length 来跳过每个完整的码点。
  for (size_t i = 0; i < l; i += codepoint_length(s8 + i, l - i)) {
    count++;
  }
  // 计数并返回码点总数。
  return count;
}

// 将单个 Unicode 码点（char32_t）编码为 UTF-8 字符串。
// 如果码点小于 0x0080（128），它是一个单字节的 ASCII 字符。
// 如果码点在 0x0800 和 0xD800 之间，它将被编码为三个字节，
// 第一个字节以 0xE0（1110 0000）开头，后两个字节以 0x80（1000 0000）开头。
inline size_t encode_codepoint(char32_t cp, char* buff) {
  // 根据码点的大小将其编码为1到4个字节。
  // 排除了非法的 Unicode 范围（例如0xD800到0xDFFF）。
  if (cp < 0x0080) {
    buff[0] = static_cast<char>(cp & 0x7F);
    return 1;
  } else if (cp < 0x0800) {
    buff[0] = static_cast<char>(0xC0 | ((cp >> 6) & 0x1F));
    buff[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  } else if (cp < 0xD800) {
    buff[0] = static_cast<char>(0xE0 | ((cp >> 12) & 0xF));
    buff[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    buff[2] = static_cast<char>(0x80 | (cp & 0x3F));
    return 3;
  } else if (cp < 0xE000) {
    // D800 - DFFF is invalid...
    return 0;
  } else if (cp < 0x10000) {
    buff[0] = static_cast<char>(0xE0 | ((cp >> 12) & 0xF));
    buff[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    buff[2] = static_cast<char>(0x80 | (cp & 0x3F));
    return 3;
  } else if (cp < 0x110000) {
    buff[0] = static_cast<char>(0xF0 | ((cp >> 18) & 0x7));
    buff[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    buff[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    buff[3] = static_cast<char>(0x80 | (cp & 0x3F));
    return 4;
  }
  // 返回编码后的字节长度。
  return 0;
}

// 将单个 Unicode 码点（char32_t）编码为 UTF-8 字符串。
inline std::string encode_codepoint(char32_t cp) {
  char buff[4];
  auto l = encode_codepoint(cp, buff);
  return std::string(buff, l);
}

// 将 UTF-8 编码的字符串解码为单个 Unicode 码点。
inline bool decode_codepoint(const char* s8, size_t l, size_t& bytes,
                             char32_t& cp) {
  if (l) {
    auto b = static_cast<uint8_t>(s8[0]);
    if ((b & 0x80) == 0) {
      bytes = 1;
      cp = b;
      return true;
    } else if ((b & 0xE0) == 0xC0) {
      if (l >= 2) {
        bytes = 2;
        cp = ((static_cast<char32_t>(s8[0] & 0x1F)) << 6) |
             (static_cast<char32_t>(s8[1] & 0x3F));
        return true;
      }
    } else if ((b & 0xF0) == 0xE0) {
      if (l >= 3) {
        bytes = 3;
        cp = ((static_cast<char32_t>(s8[0] & 0x0F)) << 12) |
             ((static_cast<char32_t>(s8[1] & 0x3F)) << 6) |
             (static_cast<char32_t>(s8[2] & 0x3F));
        return true;
      }
    } else if ((b & 0xF8) == 0xF0) {
      if (l >= 4) {
        bytes = 4;
        cp = ((static_cast<char32_t>(s8[0] & 0x07)) << 18) |
             ((static_cast<char32_t>(s8[1] & 0x3F)) << 12) |
             ((static_cast<char32_t>(s8[2] & 0x3F)) << 6) |
             (static_cast<char32_t>(s8[3] & 0x3F));
        return true;
      }
    }
  }
  return false;
}

inline size_t decode_codepoint(const char* s8, size_t l, char32_t& cp) {
  size_t bytes;
  if (decode_codepoint(s8, l, bytes, cp)) {
    return bytes;
  }
  return 0;
}

inline char32_t decode_codepoint(const char* s8, size_t l) {
  char32_t cp = 0;
  decode_codepoint(s8, l, cp);
  return cp;
}

// 将完整的 UTF-8 编码字符串解码为 Unicode 码点序列。
inline std::u32string decode(const char* s8, size_t l) {
  std::u32string out;
  size_t i = 0;
  while (i < l) {
    auto beg = i++;
    while (i < l && (s8[i] & 0xc0) == 0x80) {
      i++;
    }
    out += decode_codepoint(&s8[beg], (i - beg));
  }
  return out;
}

template <typename T>
const char* u8(const T* s) {
  return reinterpret_cast<const char*>(s);
}

/*-----------------------------------------------------------------------------
 *  escape_characters
 *---------------------------------------------------------------------------*/

// 将字符串中的特殊字符转换为它们的转义序列表示。
inline std::string escape_characters(const char *s, size_t n) {
  std::string str;
  for (size_t i = 0; i < n; i++) {
    auto c = s[i];
    switch (c) {
    case '\f': str += "\\f"; break;
    case '\n': str += "\\n"; break;
    case '\r': str += "\\r"; break;
    case '\t': str += "\\t"; break;
    case '\v': str += "\\v"; break;
    default: str += c; break;
    }
  }
  return str;
}

inline std::string escape_characters(std::string_view sv) {
  return escape_characters(sv.data(), sv.size());
}

/*-----------------------------------------------------------------------------
 *  resolve_escape_sequence
 *---------------------------------------------------------------------------*/

inline bool is_hex(char c, int &v) {
  if ('0' <= c && c <= '9') {
    v = c - '0';
    return true;
  } else if ('a' <= c && c <= 'f') {
    v = c - 'a' + 10;
    return true;
  } else if ('A' <= c && c <= 'F') {
    v = c - 'A' + 10;
    return true;
  }
  return false;
}

inline bool is_digit(char c, int &v) {
  if ('0' <= c && c <= '9') {
    v = c - '0';
    return true;
  }
  return false;
}

inline std::pair<int, size_t> parse_hex_number(const char *s, size_t n,
                                               size_t i) {
  int ret = 0;
  int val;
  while (i < n && is_hex(s[i], val)) {
    ret = static_cast<int>(ret * 16 + val);
    i++;
  }
  return std::pair(ret, i);
}

inline std::pair<int, size_t> parse_octal_number(const char *s, size_t n,
                                                 size_t i) {
  int ret = 0;
  int val;
  while (i < n && is_digit(s[i], val)) {
    ret = static_cast<int>(ret * 8 + val);
    i++;
  }
  return std::pair(ret, i);
}

// 解析包含转义序列的字符串，并将这些序列转换回它们对应的字符。
inline std::string resolve_escape_sequence(const char *s, size_t n) {
  std::string r;
  r.reserve(n);

  size_t i = 0;
  while (i < n) {
    auto ch = s[i];
    if (ch == '\\') {
      i++;
      if (i == n) { throw std::runtime_error("Invalid escape sequence..."); }
      switch (s[i]) {
      case 'f':
        r += '\f';
        i++;
        break;
      case 'n':
        r += '\n';
        i++;
        break;
      case 'r':
        r += '\r';
        i++;
        break;
      case 't':
        r += '\t';
        i++;
        break;
      case 'v':
        r += '\v';
        i++;
        break;
      case '\'':
        r += '\'';
        i++;
        break;
      case '"':
        r += '"';
        i++;
        break;
      case '[':
        r += '[';
        i++;
        break;
      case ']':
        r += ']';
        i++;
        break;
      case '\\':
        r += '\\';
        i++;
        break;
      case 'x':
      case 'u': {
        char32_t cp;
        std::tie(cp, i) = parse_hex_number(s, n, i + 1);
        r += encode_codepoint(cp);
        break;
      }
      default: {
        char32_t cp;
        std::tie(cp, i) = parse_octal_number(s, n, i);
        r += encode_codepoint(cp);
        break;
      }
      }
    } else {
      r += ch;
      i++;
    }
  }
  return r;
}

/*-----------------------------------------------------------------------------
 *  token_to_number_ - This function should be removed eventually
 *---------------------------------------------------------------------------*/

// 将字符串转换为数字。
template <typename T> T token_to_number_(std::string_view sv) {
  T n = 0;
#if __has_include(<charconv>)
  if constexpr (!std::is_floating_point<T>::value) {
    std::from_chars(sv.data(), sv.data() + sv.size(), n);
#else
  if constexpr (false) {
#endif
  } else {
    auto s = std::string(sv);
    std::istringstream ss(s);
    ss >> n;
  }
  return n;
}

/*-----------------------------------------------------------------------------
 *  Trie
 *---------------------------------------------------------------------------*/

class Trie {
public:
  Trie() = default;
  Trie(const Trie &) = default;

  Trie(const std::vector<std::string> &items) {
    for (const auto &item : items) {
      for (size_t len = 1; len <= item.size(); len++) {
        auto last = len == item.size();
        std::string_view sv(item.data(), len);
        auto it = dic_.find(sv);
        if (it == dic_.end()) {
          dic_.emplace(sv, Info{last, last});
        } else if (last) {
          it->second.match = true;
        } else {
          it->second.done = false;
        }
      }
    }
  }

  size_t match(const char *text, size_t text_len) const {
    size_t match_len = 0;
    auto done = false;
    size_t len = 1;
    while (!done && len <= text_len) {
      std::string_view sv(text, len);
      auto it = dic_.find(sv);
      if (it == dic_.end()) {
        done = true;
      } else {
        if (it->second.match) { match_len = len; }
        if (it->second.done) { done = true; }
      }
      len += 1;
    }
    return match_len;
  }

private:
  struct Info {
    bool done;
    bool match;
  };

  // TODO: Use unordered_map when heterogeneous lookup is supported in C++20
  // std::unordered_map<std::string, Info> dic_;
  std::map<std::string, Info, std::less<>> dic_;
};

}  // namespace peg

#endif  // PEG_H