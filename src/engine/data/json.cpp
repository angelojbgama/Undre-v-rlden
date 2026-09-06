#include "engine/data/json.h"
#include <charconv>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace underworld::engine::data {
const JsonValue* JsonValue::find(std::string_view key) const noexcept {
    const auto* object = std::get_if<JsonObject>(&value); if (!object) return nullptr;
    for (const auto& member : *object) if (member.first == key) return &member.second;
    return nullptr;
}
namespace {
class Parser final {
public:
    explicit Parser(std::string_view input) : input_(input) {}
    JsonParseResult run() {
        JsonParseResult result; skip(); auto value = parse(0);
        if (value && atEnd()) result.value = std::make_unique<JsonValue>(std::move(*value));
        else if (value) fail("trailing characters after JSON value");
        result.diagnostics = std::move(errors_); return result;
    }
private:
    bool atEnd() const noexcept { return position_ == input_.size(); }
    char peek() const noexcept { return atEnd() ? '\0' : input_[position_]; }
    void advance() noexcept { if (!atEnd()) { if (input_[position_] == '\n') { ++line_; column_ = 1; } else ++column_; ++position_; } }
    void skip() noexcept { while (std::isspace(static_cast<unsigned char>(peek())) != 0) advance(); }
    void fail(std::string message) { errors_.push_back({{position_, line_, column_}, std::move(message)}); }
    bool take(char c) { if (peek() != c) return false; advance(); return true; }
    std::optional<JsonValue> parse(std::size_t depth) {
        if (depth > 128) { fail("maximum nesting depth exceeded"); return std::nullopt; }
        skip(); const auto begin = JsonSourceLocation{position_, line_, column_};
        std::optional<JsonValue> result;
        if (peek() == '{') { auto v = object(depth); if (v) result = JsonValue{{begin, {position_, line_, column_}}, std::move(*v)}; }
        else if (peek() == '[') { auto v = array(depth); if (v) result = JsonValue{{begin, {position_, line_, column_}}, std::move(*v)}; }
        else if (peek() == '"') { auto s = string(); if (s) result = JsonValue{{begin, {position_, line_, column_}}, std::move(*s)}; }
        else if (peek() == '-' || std::isdigit(static_cast<unsigned char>(peek())) != 0) { auto n = number(); if (n) result = JsonValue{{begin, {position_, line_, column_}}, JsonNumber{std::move(*n)}}; }
        else if (input_.substr(position_, 4) == "true") { for (int i=0;i<4;++i) advance(); result = JsonValue{{begin,{position_,line_,column_}},true}; }
        else if (input_.substr(position_, 5) == "false") { for (int i=0;i<5;++i) advance(); result = JsonValue{{begin,{position_,line_,column_}},false}; }
        else if (input_.substr(position_, 4) == "null") { for (int i=0;i<4;++i) advance(); result = JsonValue{{begin,{position_,line_,column_}},nullptr}; }
        else { fail("expected JSON value"); return std::nullopt; }
        skip(); return result;
    }
    std::optional<JsonArray> array(std::size_t depth) {
        advance(); JsonArray result; skip(); if (take(']')) return result;
        while (true) { auto item = parse(depth + 1); if (!item) return std::nullopt; result.push_back(std::move(*item)); skip(); if (take(']')) return result; if (!take(',')) { fail("expected ',' or ']' in array"); return std::nullopt; } skip(); if (peek() == ']') { fail("trailing comma in array"); return std::nullopt; } }
    }
    std::optional<JsonObject> object(std::size_t depth) {
        advance(); JsonObject result; skip(); if (take('}')) return result;
        while (true) {
            if (peek() != '"') { fail("expected object member name"); return std::nullopt; }
            auto key = string(); if (!key) return std::nullopt; skip(); if (!take(':')) { fail("expected ':' after object member name"); return std::nullopt; }
            auto value = parse(depth + 1); if (!value) return std::nullopt;
            for (const auto& member : result) if (member.first == *key) { fail("duplicate object key"); return std::nullopt; }
            result.emplace_back(std::move(*key), std::move(*value)); skip(); if (take('}')) return result; if (!take(',')) { fail("expected ',' or '}' in object"); return std::nullopt; } skip(); if (peek() == '}') { fail("trailing comma in object"); return std::nullopt; }
        }
    }
    std::optional<std::string> string() {
        advance(); std::string result;
        while (!atEnd() && peek() != '"') {
            const unsigned char c = static_cast<unsigned char>(peek());
            if (c < 0x20) { fail("unescaped control character in string"); return std::nullopt; }
            if (c != '\\') {
                if (c >= 0x80) {
                    const auto needed = c < 0xE0 ? 1u : c < 0xF0 ? 2u : c < 0xF8 ? 3u : 0u;
                    if (needed == 0) { fail("invalid UTF-8 leading byte"); return std::nullopt; }
                    std::uint32_t code = c & ((1u << (7u - needed)) - 1u);
                    advance();
                    for (unsigned i = 0; i < needed; ++i) {
                        const auto next = static_cast<unsigned char>(peek());
                        if ((next & 0xC0u) != 0x80u) { fail("invalid UTF-8 continuation byte"); return std::nullopt; }
                        code = (code << 6u) | (next & 0x3Fu); advance();
                    }
                    if ((needed == 1 && code < 0x80u) || (needed == 2 && code < 0x800u) ||
                        (needed == 3 && code < 0x10000u) || code > 0x10FFFFu ||
                        (code >= 0xD800u && code <= 0xDFFFu)) {
                        fail("invalid UTF-8 code point"); return std::nullopt;
                    }
                    result.append(input_.substr(position_ - needed - 1, needed + 1)); continue;
                }
                result.push_back(static_cast<char>(c)); advance(); continue;
            }
            advance(); const char escaped = peek(); advance();
            if (escaped == '"' || escaped == '\\' || escaped == '/') result.push_back(escaped);
            else if (escaped == 'b') result.push_back('\b'); else if (escaped == 'f') result.push_back('\f'); else if (escaped == 'n') result.push_back('\n'); else if (escaped == 'r') result.push_back('\r'); else if (escaped == 't') result.push_back('\t');
            else if (escaped == 'u') { unsigned code{}; for (int i=0;i<4;++i) { const char h=peek(); if (!std::isxdigit(static_cast<unsigned char>(h))) { fail("invalid unicode escape"); return std::nullopt; } code = code*16 + static_cast<unsigned>(std::stoi(std::string(1,h), nullptr, 16)); advance(); } if (code >= 0xD800 && code <= 0xDBFF) { if (input_.substr(position_, 2) != "\\u") { fail("unpaired surrogate"); return std::nullopt; } advance(); advance(); unsigned low{}; for (int i=0;i<4;++i) { const char h=peek(); if (!std::isxdigit(static_cast<unsigned char>(h))) { fail("invalid unicode escape"); return std::nullopt; } low=low*16+static_cast<unsigned>(std::stoi(std::string(1,h),nullptr,16)); advance(); } if (low < 0xDC00 || low > 0xDFFF) { fail("invalid surrogate pair"); return std::nullopt; } code=0x10000+((code-0xD800)<<10)+(low-0xDC00); } else if (code >= 0xDC00) { fail("unpaired surrogate"); return std::nullopt; } if (code < 0x80) result.push_back(static_cast<char>(code)); else if (code < 0x800) { result.push_back(static_cast<char>(0xC0|(code>>6))); result.push_back(static_cast<char>(0x80|(code&63))); } else if (code < 0x10000) { result.push_back(static_cast<char>(0xE0|(code>>12))); result.push_back(static_cast<char>(0x80|((code>>6)&63))); result.push_back(static_cast<char>(0x80|(code&63))); } else { result.push_back(static_cast<char>(0xF0|(code>>18))); result.push_back(static_cast<char>(0x80|((code>>12)&63))); result.push_back(static_cast<char>(0x80|((code>>6)&63))); result.push_back(static_cast<char>(0x80|(code&63))); } }
            else { fail("invalid string escape"); return std::nullopt; }
        }
        if (!take('"')) { fail("unterminated string"); return std::nullopt; } return result;
    }
    std::optional<std::string> number() {
        const auto start=position_; if (take('-')) {} if (peek()=='0') advance(); else if (std::isdigit(static_cast<unsigned char>(peek())) != 0) while (std::isdigit(static_cast<unsigned char>(peek())) != 0) advance(); else { fail("invalid number"); return std::nullopt; }
        if (take('.')) { if (std::isdigit(static_cast<unsigned char>(peek())) == 0) { fail("invalid number fraction"); return std::nullopt; } while (std::isdigit(static_cast<unsigned char>(peek())) != 0) advance(); }
        if (peek()=='e'||peek()=='E') { advance(); if (peek()=='+'||peek()=='-') advance(); if (std::isdigit(static_cast<unsigned char>(peek())) == 0) { fail("invalid number exponent"); return std::nullopt; } while (std::isdigit(static_cast<unsigned char>(peek())) != 0) advance(); }
        if (std::isdigit(static_cast<unsigned char>(peek())) != 0 || peek()=='.') { fail("invalid number"); return std::nullopt; } return std::string(input_.substr(start, position_-start));
    }
    std::string_view input_; std::size_t position_{}; std::size_t line_{1}; std::size_t column_{1}; std::vector<JsonParseDiagnostic> errors_;
};
void writeString(std::string& out, std::string_view value) { out.push_back('"'); for (unsigned char c : value) { switch(c) { case '"': out += "\\\""; break; case '\\': out += "\\\\"; break; case '\n': out += "\\n"; break; case '\r': out += "\\r"; break; case '\t': out += "\\t"; break; default: if(c<0x20) { std::ostringstream s; s<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<static_cast<int>(c); out+=s.str(); } else out.push_back(static_cast<char>(c)); } } out.push_back('"'); }
void write(std::string& out, const JsonValue& value, bool pretty, int depth) { const auto indent=[&](){ if(pretty) out.append(static_cast<std::size_t>(depth*2),' '); }; if(std::holds_alternative<std::nullptr_t>(value.value)) out+="null"; else if(auto b=std::get_if<bool>(&value.value)) out+=*b?"true":"false"; else if(auto n=std::get_if<JsonNumber>(&value.value)) out+=n->lexeme; else if(auto s=std::get_if<std::string>(&value.value)) writeString(out,*s); else if(auto a=std::get_if<JsonArray>(&value.value)){out+='['; for(std::size_t i=0;i<a->size();++i){if(i){out+=',';}if(pretty)out+='\n';indent();write(out,(*a)[i],pretty,depth+1);}if(!a->empty()&&pretty){out+='\n';out.append(static_cast<std::size_t>(depth*2),' ');}out+=']';} else {const auto& o=std::get<JsonObject>(value.value);out+='{';for(std::size_t i=0;i<o.size();++i){if(i)out+=',';if(pretty)out+='\n';indent();writeString(out,o[i].first);out+=pretty?": ":":";write(out,o[i].second,pretty,depth+1);}if(!o.empty()&&pretty){out+='\n';out.append(static_cast<std::size_t>(depth*2),' ');}out+='}';}}
}
std::string writeJson(const JsonValue& value, bool pretty) { std::string result; write(result,value,pretty,0); if(pretty) result+='\n'; return result; }
JsonParseResult parseJson(std::string_view text) { return Parser{text}.run(); }
} // namespace underworld::engine::data
