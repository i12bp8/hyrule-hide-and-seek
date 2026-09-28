#pragma once

// Just enough JSON for the relay's control messages and the public room list.

#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace hs::json {

struct Value {
    enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Value> array;
    std::map<std::string, Value> object;

    const Value& operator[](const char* key) const {
        static const Value null;
        if (kind != Kind::Object) return null;
        auto it = object.find(key);
        return it != object.end() ? it->second : null;
    }
    const std::string& str() const { return string; }
    int num() const { return static_cast<int>(number); }
    bool is_object() const { return kind == Kind::Object; }
};

class Parser {
public:
    explicit Parser(std::string_view text) : m_text(text) {}

    bool parse(Value& out) {
        if (!value(out, 0)) return false;
        ws();
        return m_pos == m_text.size();
    }

private:
    bool value(Value& out, int depth) {
        if (depth > 16) return false;
        ws();
        if (m_pos >= m_text.size()) return false;
        const char c = m_text[m_pos];
        if (c == '{') return object(out, depth);
        if (c == '[') return array(out, depth);
        if (c == '"') {
            out.kind = Value::Kind::String;
            return string(out.string);
        }
        if (literal("true")) {
            out.kind = Value::Kind::Bool;
            out.boolean = true;
            return true;
        }
        if (literal("false")) {
            out.kind = Value::Kind::Bool;
            return true;
        }
        if (literal("null")) return true;
        return number(out);
    }

    bool object(Value& out, int depth) {
        out.kind = Value::Kind::Object;
        ++m_pos;
        ws();
        if (eat('}')) return true;
        for (;;) {
            ws();
            std::string key;
            if (!string(key)) return false;
            ws();
            if (!eat(':')) return false;
            Value v;
            if (!value(v, depth + 1)) return false;
            out.object[std::move(key)] = std::move(v);
            ws();
            if (eat(',')) continue;
            return eat('}');
        }
    }

    bool array(Value& out, int depth) {
        out.kind = Value::Kind::Array;
        ++m_pos;
        ws();
        if (eat(']')) return true;
        for (;;) {
            Value v;
            if (!value(v, depth + 1)) return false;
            out.array.push_back(std::move(v));
            ws();
            if (eat(',')) continue;
            return eat(']');
        }
    }

    bool string(std::string& out) {
        if (!eat('"')) return false;
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (m_pos >= m_text.size()) return false;
            const char e = m_text[m_pos++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                if (m_pos + 4 > m_text.size()) return false;
                const unsigned cp = std::strtoul(std::string(m_text.substr(m_pos, 4)).c_str(), nullptr, 16);
                m_pos += 4;
                if (cp < 0x80) {
                    out += static_cast<char>(cp);
                } else if (cp < 0x800) {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                } else {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                break;
            }
            default: out += e; break;
            }
        }
        return false;
    }

    bool number(Value& out) {
        const size_t start = m_pos;
        while (m_pos < m_text.size() && std::string_view("+-0123456789.eE").find(m_text[m_pos]) !=
                                            std::string_view::npos) {
            ++m_pos;
        }
        if (m_pos == start) return false;
        out.kind = Value::Kind::Number;
        out.number = std::strtod(std::string(m_text.substr(start, m_pos - start)).c_str(), nullptr);
        return true;
    }

    bool literal(std::string_view word) {
        if (m_text.substr(m_pos, word.size()) != word) return false;
        m_pos += word.size();
        return true;
    }

    void ws() {
        while (m_pos < m_text.size() &&
               (m_text[m_pos] == ' ' || m_text[m_pos] == '\n' || m_text[m_pos] == '\r' ||
                   m_text[m_pos] == '\t')) {
            ++m_pos;
        }
    }

    bool eat(char c) {
        if (m_pos < m_text.size() && m_text[m_pos] == c) {
            ++m_pos;
            return true;
        }
        return false;
    }

    std::string_view m_text;
    size_t m_pos = 0;
};

inline bool parse(std::string_view text, Value& out) {
    return Parser(text).parse(out);
}

inline std::string escape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}

}  // namespace hs::json
