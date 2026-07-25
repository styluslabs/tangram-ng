#include "yaml.h"
#include <stdlib.h>
#include <string.h>
#include <climits>
#include <vector>
#include <utility>
#include <algorithm>
#include <fstream>
#include <sstream>
#ifdef GAML_DOUBLE_CONV
#include "double-conversion.h"
#endif

// based on https://github.com/vivkin/gason

// Why this was written:
// 1. NIH syndrome
// 2. data race in tangrams/yaml-cpp causing crashes (due to custom, non-atomic ref counting)
//  - upstream yaml-cpp uses shared_ptr instead and probably would have fixed the issue, but
//   divergence between branches may have made swapping it in difficult
// 3. significant reduction in LOC by replacing yaml-cpp and rapidjson

#ifndef GAML_LOG
#define GAML_LOG(msg, ...) do { fprintf(stderr, msg, ## __VA_ARGS__); } while (0)
#endif

namespace YAML {

// helper fns

static inline bool isspace(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

static inline bool isdelim(char c) {
    return c == ',' || c == ':' || c == ']' || c == '}' || isspace(c) || c == '#' || !c;
}

static inline bool isflowdelim(char c) {
    return c == ',' || c == ']' || c == '}' || c == '[' || c == '{';
}

// YAML allows newlines in unquoted strings, but we don't
static inline bool isendscalar(char c, char d) {
    return c == '\r' || c == '\n' || (c == ':' && isspace(d)) || (d == '#' && isspace(c));
}

static inline bool isarray(char c, char d) { return c == '-' && isspace(d); }

static inline bool iskeydelim(char c, char d) { return c == ':' && isspace(d); }

static inline bool isdigit(char c) { return c >= '0' && c <= '9'; }

static inline bool isxdigit(char c) {
    return (c >= '0' && c <= '9') || ((c & ~' ') >= 'A' && (c & ~' ') <= 'F');
}

static inline int char2int(char c) {
    if (c <= '9')
        return c - '0';
    return (c & ~' ') - 'A' + 10;
}

static double string2double(const char *s, char **endptr) {
    char ch = *s;
    if (ch == '-')
        ++s;

    double result = 0;
    while (isdigit(*s))
        result = (result * 10) + (*s++ - '0');

    if (*s == '.') {
        ++s;

        double fraction = 1;
        while (isdigit(*s)) {
            fraction *= 0.1;
            result += (*s++ - '0') * fraction;
        }
    }

    if (*s == 'e' || *s == 'E') {
        ++s;

        double base = 10;
        if (*s == '+')
            ++s;
        else if (*s == '-') {
            ++s;
            base = 0.1;
        }

        unsigned int exponent = 0;
        while (isdigit(*s))
            exponent = (exponent * 10) + (*s++ - '0');

        double power = 1;
        for (; exponent; exponent >>= 1, base *= base)
            if (exponent & 1)
                power *= base;

        result *= power;
    }

    *endptr = (char*)s;
    return ch == '-' ? -result : result;
}

// float to string is non-trivial
// maybe use github.com/abolz/Drachennest (dragonbox) or github.com/miloyip/dtoa-benchmark
static std::string double2string(double f, unsigned int prec = 16) {
#ifdef GAML_DOUBLE_CONV
    using namespace double_conversion;
    static DoubleToStringConverter D2S(
        DoubleToStringConverter::UNIQUE_ZERO | DoubleToStringConverter::EMIT_POSITIVE_EXPONENT_SIGN,
        "inf", "nan", 'e', -6, 21, 6, 0);

    char buffer[256];
    StringBuilder builder(buffer, 256);
    D2S.ToShortest(f, &builder);
    return std::string(builder.Finalize());
#else
    char buff[64];
    snprintf(buff, 64, "%g", f);
    return std::string(buff);
#endif
}

static inline ListNode* insertAfter(ListNode* tail, ListNode* node) {
    if (!tail) { return node->next = node; }
    node->next = tail->next;
    tail->next = node;
    return node;
}

static inline Node listToValue(Tag tag, ListNode* tail) {
    if (tail) {
        auto head = tail->next;
        tail->next = nullptr;
        return Node(tag, head);
    }
    return Node(tag, nullptr);
}

// Node

static Node UNDEFINED_VALUE(Tag::UNDEFINED);
static Node INVALID_VALUE(Tag::INVALID);

Node::Node(std::string&& s, Tag flags) : strVal(std::move(s)), flags_(flags) {
    if (getTag() == Tag::UNDEFINED) { flags_ = flags | Tag::STRING; }
}

Node::Node(std::initializer_list<InitPair> items) {
    ListNode* tail = nullptr;
    for (auto& item : items) {
        tail = insertAfter(tail, new ListNode{item.val.clone(), nullptr, item.key});
    }
    *this = listToValue(Tag::OBJECT, tail);
}

Node Array(std::initializer_list<Node> items) {
    ListNode* tail = nullptr;
    for (auto& item : items) {
        tail = insertAfter(tail, new ListNode{item.clone(), nullptr, {}});
    }
    return listToValue(Tag::ARRAY, tail);
}

Node::~Node() {
    if (getTag() == Tag::ARRAY || getTag() == Tag::OBJECT) {
        while (pval_) { delete std::exchange(pval_, pval_->next);  }
    }
}

ConstPairItems Node::const_pairs() const { return ConstPairItems{getNode()}; }
ConstPairItems Node::pairs() const { return const_pairs(); }
PairItems Node::pairs() { return PairItems{getNode()}; }
ListItems Node::items() const { return ListItems{getNode()}; }

Node& Node::operator=(Node&& b) {
    if(this == &UNDEFINED_VALUE || this == &INVALID_VALUE) { assert(false); return *this; }
    std::swap(pval_, b.pval_);
    std::swap(flags_, b.flags_);
    std::swap(strVal, b.strVal);
    return *this;
}

Node Node::clone() const {
    if (!getNode()) {
        Node res(flags_);
        res.strVal = strVal;
        res.fval_ = fval_;
        return res;
    }
    ListNode* tail = nullptr;
    for (auto item : items()) {
        tail = insertAfter(tail, new ListNode{item->value.clone(), nullptr, item->key.clone()});
    }
    return listToValue(flags_, tail);
}

Node& Node::add(const char* key, bool replace) {
    assert(key && key[0]);
    Node& n = const_cast<Node&>(const_cast<const Node&>(*this)[key]);
    if (&n == &UNDEFINED_VALUE) {
        if (getTag() == Tag::UNDEFINED) { flags_ = Tag::OBJECT; }
        auto* val = new ListNode{Tag::UNDEFINED, nullptr, key};
        ListNode* obj = getNode();
        if (!obj) {
            pval_ = val;
        } else {
            while (obj->next) { obj = obj->next; }
            obj->next = val;
        }
        return val->value;
    } else if (replace && &n != &INVALID_VALUE) {
        n = Node();
    }
    return n;
}

const Node& Node::at(const char* key) const {
    if (getTag() == Tag::UNDEFINED) { return UNDEFINED_VALUE; }
    if (getTag() != Tag::OBJECT) { return INVALID_VALUE; }
    ListNode* obj = pval_;
    while (obj && obj->key != key) {
      obj = obj->next;
    }
    return obj ? obj->value : UNDEFINED_VALUE;
}

const Node& Node::at(int idx) const {
    if (getTag() == Tag::UNDEFINED) { return UNDEFINED_VALUE; }
    if (getTag() != Tag::ARRAY) { return INVALID_VALUE; }
    ListNode* array = getNode();
    do {
        while (array && array->value.getTag() == Tag::YAML_COMMENT) { array = array->next; }
    } while (idx-- && array && (array = array->next));
    return array ? array->value : UNDEFINED_VALUE;
}

Node& Node::operator[](int idx) {
    int n = size();
    if (idx > n) { return INVALID_VALUE; }
    if (idx == n) { return push_back(Node()); }
    return const_cast<Node&>(const_cast<const Node&>(*this)[idx]);
}

bool Node::remove(const char* key) {
    if (getTag() != Tag::OBJECT) { return false; }

    ListNode** obj = &pval_;
    while (*obj && (*obj)->key != key) {
        obj = &(*obj)->next;
    }
    if (*obj) {
        delete std::exchange(*obj, (*obj)->next);
    }
    return bool(*obj);
}

Node& Node::insert(int idx, Node&& val) {
    if (this != &UNDEFINED_VALUE && getTag() == Tag::UNDEFINED) { flags_ = Tag::ARRAY; }
    if (getTag() != Tag::ARRAY) { return INVALID_VALUE; }
    ListNode* item = new ListNode{std::move(val), nullptr, {}};
    ListNode* array = pval_;
    if (!array || idx == 0) { item->next = array; pval_ = item; return item->value; }
    for (int ii = 1; ii < idx && array->next; ++ii) { array = array->next; }
    item->next = array->next;
    array->next = item;
    return item->value;
}

bool Node::remove(int idx) {
    if (getTag() != Tag::ARRAY || !pval_) { return false; }
    if (idx == 0) {
        delete std::exchange(pval_, pval_->next);
        return true;
    }
    ListNode* obj = pval_;
    while (obj && --idx) { obj = obj->next; }  // note pre-decrement to get node before [idx]
    if (obj && obj->next) {
        delete std::exchange(obj->next, obj->next->next);
        return true;
    }
    return false;
}

void Node::merge(Node&& src) {
    if (src.getTag() != Tag::OBJECT || !src.getNode() || this == &UNDEFINED_VALUE) {
      return; }
    if (getTag() != Tag::UNDEFINED && getTag() != Tag::OBJECT) {
      return; }
    for (auto other : src.items()) {
        Node& ours = add(other->key.getString());
        if (ours.getTag() == Tag::OBJECT && other->value.getTag() == Tag::OBJECT) {
            ours.merge(std::move(other->value));
        }  else {
            ours = std::move(other->value);
        }
    }
}

int Node::size() const {
    int n = 0;
    for (ListNode* obj = getNode(); obj; obj = obj->next) { ++n; }
    return n;
}

template<> double Node::as(double _default, bool* ok) const {
    if (ok) { *ok = true; }
    if (isNumber() || getTag() == Tag::JSON_BOOL) { return fval_; }
    if (getTag() == Tag::STRING && (getFlags() & Tag::YAML_STRINGMASK) == Tag::YAML_UNQUOTED) {
        char* endptr = nullptr;
        auto s = getString();
        double val = _default;
        if (s.size() > 2 && s[0] == '0' && s[1] != '.') {
            if (s[1] == 'x') { val = strtoul(&s[2], &endptr, 16); }
            else if (s[1] == 'b') { val = strtoul(&s[2], &endptr, 2); }
            else if (s[1] == 'o') { val = strtoul(&s[2], &endptr, 8); }
        } else if (!s.empty()) {
            val = string2double(s.c_str(), &endptr);
        }
        // endptr should point to '\0' terminator char after successful conversion
        if (endptr && *endptr == '\0') { return val; }
    }
    if (ok) { *ok = false; }
    return _default;
}

template<> std::string Node::as(std::string _default, bool* ok) const {
    if (ok) { *ok = true; }
    if (getTag() == Tag::JSON_BOOL) { return fval_ != 0.0 ? "true" : "false"; }
    if (isNumber()) { return double2string(getNumber()); }
    if (getTag() == Tag::STRING) { return getString(); }
    if (ok) { *ok = false; }
    return _default;
}

template<> bool Node::as(bool _default, bool* ok) const {
    // YAML 1.2 only allows true/false
    static const char* boolstrs[] = {"true","false","True","False","TRUE","FALSE"};
    //"y","n","Y","N","yes","no","Yes","No","YES","NO","on","off","On","Off","ON","OFF"};

    if (ok) { *ok = true; }
    if (isNumber() || getTag() == Tag::JSON_BOOL) { return fval_ != 0; }
    if (getTag() == Tag::STRING && ((getFlags() & Tag::YAML_STRINGMASK) == Tag::YAML_UNQUOTED)) {
        int idx = 0;
        for (const char* s : boolstrs) {
            if (strVal == s) { return idx%2 == 0; }
            ++idx;
        }
    }
    if (ok) { *ok = false; }
    return  _default;
}

// only for yaml-cpp compatibility

bool Node::IsNull() const {
    static const char* nullstrs[] = {"~", "null","Null","NULL"};

    if (getTag() == Tag::STRING && ((getFlags() & Tag::YAML_STRINGMASK) == Tag::YAML_UNQUOTED)) {
        for (const char* s : nullstrs) {
            if (strVal == s) { return true; }
        }
    }
    return false;
}

NodeType Node::Type() const {
    switch(getTag()) {
        case Tag::STRING:
        case Tag::NUMBER: return NodeType::Scalar;
        case Tag::ARRAY: return NodeType::Sequence;
        case Tag::OBJECT: return NodeType::Map;
        default: return NodeType::Undefined;  // Null?
    }
}

void Node::setNoWrite(bool nowrite) {
    //if (value == &UNDEFINED_VALUE || value == &INVALID_VALUE) { return; }
    flags_ = nowrite ? (flags_ | Tag::NO_WRITE) : (flags_ & ~Tag::NO_WRITE);
}

ConstNodeIterator Node::begin() const { return ConstNodeIterator{getNode()}; }
ConstNodeIterator Node::end() const { return ConstNodeIterator{nullptr}; }
NodeIterator Node::begin() { return NodeIterator{getNode()}; }
NodeIterator Node::end() { return NodeIterator{nullptr}; }

// Parser

static inline char unescapedChar(char c) {
    switch(c) {
        case '\\':
        case '"':
        case '/':  return c;
        case 'b':  return '\b';
        case 'f':  return '\f';
        case 'n':  return '\n';
        case 'r':  return '\r';
        case 't':  return '\t';
    }
    return '\0';
}

static inline const char* escapedChar(char c) {
    switch(c) {
        case '\\':  return "\\\\";
        case '"':   return "\\\"";
        //case '/':   return "\\/"; -- can be escaped but doesn't need to be
        case '\b':  return "\\b";
        case '\f':  return "\\f";
        case '\n':  return "\\n";
        case '\r':  return "\\r";
        case '\t':  return "\\t";
    }
    return nullptr;
}

const char* jsonStrError(Error err) {
    static const char* errorMsg[] = {"ok", "bad number", "bad string", "bad identifier", "stack overflow",
        "stack underflow", "mismatched bracket", "unexpected character", "unquoted key", "breaking bad",
        "allocation failure", "invalid error code"};
    return errorMsg[std::min(size_t(err), size_t(Error::COUNT))];
}

Node parse(const std::string& s, int flags, ParseResult* resultout) {
    return parse(s.c_str(), s.size(), flags, resultout);
}

Node LoadFile(const std::string& filename) {
    std::stringstream buffer;
    {
        std::ifstream instrm(filename);
        buffer << instrm.rdbuf();
    }
    return Load(buffer.str());
}

Node parse(const char* s, size_t len, int flags, ParseResult* resultout) {
    Node doc;
    ParseResult res = parseTo(s, len, &doc, flags);
    if (resultout) { *resultout = res; }
    if (res.error != Error::OK) {
        const char* ends = s + (len > 0 ? len : strlen(s));
        const char* newl = res.endptr;
        while (newl < ends && *newl && *newl != '\n') { ++newl; }
        GAML_LOG("YAML parse error (line %d): %s at %s\n",
                 res.linenum, jsonStrError(res.error), std::string(res.endptr, newl).c_str());
    }
    return doc;
}

ParseResult parseTo(const char *s, size_t len, Node *valueout, int flags) {
    constexpr size_t PARSE_STACK_SIZE = 32;
    ListNode* tails[PARSE_STACK_SIZE];
    Tag tags[PARSE_STACK_SIZE];
    Node keys[PARSE_STACK_SIZE];
    int indents[PARSE_STACK_SIZE];

    Node o(Tag::UNDEFINED);
    int pos = -1;
    int indent = 0;
    int flowlevel = 0;
    int linenum = 1;
    bool separator = true;
    bool startobj = false;
    char nextchar;
    ListNode* node;
    std::string temp;
    const char* s0 = s;
    const char* linestart = s;
    const char* endptr = s;
    const char* ends = len > 0 ? s + len : s + strlen(s);

    while (1) {
        while (s < ends && isspace(*s)) {
            if (*s == '\n') {
                if (!flowlevel) { linestart = s+1; }
                ++linenum;
            }
            ++s;
        }
        if (s == ends) {
            if (flowlevel || startobj) { return {Error::MISMATCH_BRACKET, linenum, s}; }
            indent = -1;
        } else {
            if (linestart) { indent = s - linestart; }
            if (*s == '{' || *s == '[') { ++flowlevel; }
        }

        if (startobj) {
            ++s;  // skip ':'
            nextchar = '{';
        } else if (flowlevel || (s < ends && *s == '#')) {
            endptr = s0 = s;
            nextchar = *s++;
        } else if (linestart && pos >= 0 && indent <= indents[pos] && keys[pos]) {
            // next non-empty, non-comment line after key has same or less indent -> key w/o value
            if (!separator || tags[pos] != Tag::OBJECT) { return {Error::UNEXPECTED_CHAR, linenum, s}; }
            o = Node("~");  // key w/o value -> null value
            nextchar = '\x7F';  // skip switch() to add object item
        } else if (pos >= 0 && indent < indents[pos]) {
            nextchar = tags[pos] == Tag::ARRAY ? ']' : '}';
        } else if (linestart && s+1 < ends && isarray(*s, s[1])) {
            if (pos < 0 || indent > indents[pos]) { nextchar = '['; }
            else if (tags[pos] == Tag::ARRAY) { ++s; continue; }
            else { return {Error::UNEXPECTED_CHAR, linenum, s}; }
        } else {
            endptr = s0 = s;
            nextchar = *s++;
            linestart = nullptr;
        }

        switch (nextchar) {
        case '\x7F': break;  // special code to skip switch ... don't want to wrap switch in if() {}
        case '"':
            ++s0;  // skip "
            for (; s < ends; ++s) {
                if (*s == '\\') {
                    temp.append(s0, s);
                    if (++s == ends || (*s == '\r' && ++s == ends)) {  // handle \ at end of line
                        return {Error::BAD_STRING, linenum, endptr};
                    }
                    if (*s == 'u') {
                        if (s + 5 > ends) { return {Error::BAD_STRING, linenum, s}; }
                        int u = 0;
                        for (int i = 0; i < 4; ++i) {
                            if (!isxdigit(*++s)) { return {Error::BAD_STRING, linenum, s}; }
                            u = u * 16 + char2int(*s);
                        }
                        if (u < 0x80) {
                            temp.push_back(u);
                        } else if (u < 0x800) {
                            temp.push_back(0xC0 | (u >> 6));
                            temp.push_back(0x80 | (u & 0x3F));
                        } else {
                            temp.push_back(0xE0 | (u >> 12));
                            temp.push_back(0x80 | ((u >> 6) & 0x3F));
                            temp.push_back(0x80 | (u & 0x3F));
                        }
                    } else if (*s == '\n') {  // don't insert anything (line continuation)
                    } else if (char c = unescapedChar(*s)) {
                        temp.push_back(c);
                    } else {
                        return {Error::BAD_STRING, linenum, s};
                    }
                    s0 = s+1;
                } else if (*s == '"') {
                    temp.append(s0, s++);
                    break;
                }
            }
            if (s < ends && !isdelim(*s)) { return {Error::BAD_STRING, linenum, s}; }
            o = Node(temp, Tag::YAML_DBLQUOTED | Tag::PARSED);
            temp.clear();
            break;
        case '[':
        case '{':
            if (++pos == PARSE_STACK_SIZE)
                return {Error::STACK_OVERFLOW, linenum, endptr};
            tails[pos] = nullptr;
            tags[pos] = (nextchar == '{' ? Tag::OBJECT : Tag::ARRAY);
            keys[pos] = startobj ? std::move(o) : Node();  //nullptr;
            indents[pos] = indent;
            separator = true;
            startobj = false;
            continue;
        case ']':
        case '}':
            if (pos == -1)
                return {Error::STACK_UNDERFLOW, linenum, endptr};
            if (tags[pos] != (nextchar == '}' ? Tag::OBJECT : Tag::ARRAY))
                return {Error::MISMATCH_BRACKET, linenum, endptr};
            if (nextchar == '}' && keys[pos])  // != nullptr)
                return {Error::UNEXPECTED_CHAR, linenum, endptr};
            if (flowlevel > 0) {
                tags[pos] = tags[pos] | Tag::YAML_FLOW;
                --flowlevel;
            }
            o = listToValue(tags[pos], tails[pos]);
            --pos;
            break;
        case ':':
            if (separator || !keys[pos]) { return {Error::UNEXPECTED_CHAR, linenum, endptr}; }
            separator = true;
            continue;
        case ',':
            if (separator || keys[pos]) { return {Error::UNEXPECTED_CHAR, linenum, endptr}; }
            separator = true;
            continue;
        case '\0':
            if (pos != -1) { return {Error::MISMATCH_BRACKET, linenum, endptr}; }
            break;
        // YAML only below
        case '#':  // comment
            while (s < ends && *s != '\r' && *s != '\n') { ++s; }
            if (flags & PARSE_COMMENTS) {
                o = Node(std::string(s0+1, s), Tag::YAML_COMMENT | Tag::PARSED);
                break;
            }
            continue;
        case '\'':  // single quoted string
            ++s0;  // skip '
            for (; s < ends; ++s) {
                char c = *s;
                if (c == '\'') {
                    // only escape sequence allowed in single quoted strings is '' -> '
                    temp.append(s0, s);
                    if (++s == ends || *s != '\'') { break; }
                    s0 = s;
                }
            }
            if (s < ends && !isdelim(*s)) { return {Error::BAD_STRING, linenum, s}; }
            o = Node(temp, Tag::YAML_SINGLEQUOTED | Tag::PARSED);
            temp.clear();
            break;
        case '|':  // literal block scalar
        case '>':  // folded block scalar
        {
            char chomp = *s;
            if (!isspace(chomp)) { ++s; }

            int blockindent = INT_MAX;
            linestart = nullptr;
            while (s < ends) {
                if (*s == '\n') {
                    if (linestart) { temp.push_back('\n'); }  // blank lines
                    linestart = ++s;
                    ++linenum;
                    continue;
                }
                if (s == ends) { break; }
                if (isspace(*s) && (++s) - linestart < blockindent) { continue; }

                if (!linestart) { return {Error::BAD_STRING, linenum, s}; }
                if (s - linestart <= indent) { break; }

                if (blockindent == INT_MAX) { blockindent = s - linestart; }
                else if (s - linestart < blockindent) { return {Error::BAD_STRING, linenum, s}; }

                s0 = s;
                while (s < ends && *s != '\r' && *s != '\n') { ++s; }
                temp.append(s0, s);

                temp.push_back(nextchar == '|' ? '\n' : ' ');
                linestart = nullptr;
            }

            if (chomp == '-') {
                temp.pop_back();
            } else if (chomp != '+') {
                while (temp.back() == '\n') { temp.pop_back(); }
                temp.push_back('\n');
            }

            o = Node(temp, Tag::YAML_BLOCKSTRING | Tag::PARSED);
            temp.clear();  // note that we do not move temp
            endptr = s;
            break;
        }
        // Unsupported YAML features
        case '?':  // mapping key
        case '&':  // node anchor
        case '*':  // alias
        case '!':  // tag handle
        case '@':  // reserved
        case '`':  // reserved
            return {Error::UNEXPECTED_CHAR, linenum, endptr};
        case '-':  // '-' could be array element, document separator, unquoted string, or number
            // '---' separates multiple documents in a single stream
            if (linestart && indent == 0 && *s == '-' && s[1] == '-') {
                if (pos != -1) { return {Error::UNEXPECTED_CHAR, linenum, endptr}; }
                s += 2;
                break;
            }
            [[fallthrough]];
        default:  // unquoted string
            if (flowlevel) {  // flow key or value
                while (s+1 < ends && !isflowdelim(*s) &&
                       !isendscalar(*s, s[1]) && !(*s == ':' && isflowdelim(s[1]))) { ++s; }
            } else {
                while (s+1 < ends && !isendscalar(*s, s[1])) { ++s; }
                if (s+1 == ends && !isendscalar(*s, '\n')) { ++s; }  // last char of single value
            }
            while (isspace(*(s-1))) { --s; }  // trim trailing spaces
            o = Node(std::string(s0, s), Tag::YAML_UNQUOTED | Tag::PARSED);
            break;
        }

        separator = false;

        // wait until we see "key: " before starting object to handle single values and objects in arrays
        if (!flowlevel && (pos < 0 || indent > indents[pos]) && s+1 < ends && iskeydelim(*s, s[1])) {
            startobj = true;
            continue;
        }

        // check for invalid JSON if requested
        if ((flags & PARSE_JSON) && o.getTag() == Tag::STRING) {
            Tag t = o.getFlags() & Tag::YAML_STRINGMASK;
            if (t == Tag::YAML_SINGLEQUOTED || t == Tag::YAML_BLOCKSTRING || t == Tag::YAML_COMMENT) {
                return {Error::UNEXPECTED_CHAR, linenum, endptr};
            }
            if (t == Tag::YAML_UNQUOTED) {
                if (o.getString() == "true") {
                    o = Node(1, Tag::JSON_BOOL | Tag::PARSED);
                } else if (o.getString() == "false") {
                    o = Node(0.0, Tag::JSON_BOOL | Tag::PARSED);
                } else if (o.getString() == "null") {
                    o = Node(Tag::JSON_NULL | Tag::PARSED);
                } else {
                    // try to parse as number
                    char* endnum;
                    double val = string2double(o.getCStr(), &endnum);
                    if (*endnum != '\0') { return {Error::BAD_NUMBER, linenum, endptr}; }
                    o = Node(val, Tag::NUMBER | Tag::PARSED);
                }
            }
        }

        if (pos == -1) {
            *valueout = std::move(o);
            return {Error::OK, linenum, s};
        }

        if (tags[pos] == Tag::OBJECT) {
            if (!keys[pos]) {
                if (o.getTag() != Tag::STRING)
                    return {Error::UNQUOTED_KEY, linenum, endptr};
                keys[pos] = std::move(o);  //o.getString().c_str();
                continue;
            }
            node = new ListNode{std::move(o), nullptr, std::move(keys[pos])};
            tails[pos] = insertAfter(tails[pos], node);
            keys[pos] = Node();
        } else {
            node = new ListNode{std::move(o), nullptr, {}};
            tails[pos] = insertAfter(tails[pos], node);
        }
    }
}

// Writer

static constexpr int YAML_KEY_STRING_LEVEL = 0x7FFFF;

static std::string escapeSingleQuoted(const std::string& str) {
    std::string res = "'";
    for (const char* s = str.data(); *s; ++s) {
        res.push_back(*s);
        if (*s == '\'') { res.push_back('\''); }
    }
    return res.append("'");
}

static std::string escapeDoubleQuoted(const std::string& str) {
    std::string res = "\"";
    for (const char* s = str.data(); *s; ++s) {
        const char* esc = escapedChar(*s);
        if (esc) { res.append(esc); } else { res.push_back(*s); }
    }
    return res.append("\"");
}

static std::string escapeQuoted(const std::string& s, char quote) {
    return quote == '"' ? escapeDoubleQuoted(s) : escapeSingleQuoted(s);
}

static std::string blockString(const std::string& str, const std::string& indent) {
    std::string res = "|\n" + indent;
    for (const char* s = str.data(); *s; ++s) {
        res.push_back(*s);
        if (*s == '\n' && s[1]) {
            res.append(indent);
        }
    }
    return res;
}

static std::string strJoin(const std::vector<std::string>& strs, const std::string& sep) {
    if (strs.empty()) { return ""; }
    size_t n = sep.size()*(strs.size()-1);
    for (auto& s : strs) { n += s.size(); }
    std::string res;
    res.reserve(n);
    for (size_t ii = 0; ii < strs.size(); ++ii) {
        if (ii > 0) { res.append(sep); }
        res.append(strs[ii]);
    }
    return res;
}

static bool skipValue(const Node& val) {
    return !val || !!(val.getFlags() & Tag::NO_WRITE);
}

std::string Writer::spacing(int level) {
    return indent > 0 && level > 0 && level < flowLevel ? std::string(indent*level, ' ') : "";
}

std::string Writer::convertArray(const Node& obj, int level) {
    std::vector<std::string> res;
    if (indent < 2 || level >= flowLevel || (obj.getFlags() & Tag::YAML_FLOW) == Tag::YAML_FLOW) {
        for (auto item : obj.items()) {
            if (skipValue(item->value)) { continue; }
            res.push_back(convert(item->value, flowLevel));
        }
        return res.empty() ? "[]" : "[" + strJoin(res, ", ") + "]";
    }
    for (auto item : obj.items()) {
        if (skipValue(item->value)) { continue; }
        std::string str = convert(item->value, level+1);
        const char* s = str.c_str();
        while (isspace(*s)) { ++s; }
        res.push_back(spacing(level) + "-" + std::string(indent-1, ' ') + s);
    }
    if(res.empty()) { return "[]"; }
    std::string block = strJoin(res, "\n");
    return level > 0 ? ("\n" + block) : (block + "\n");
}

std::string Writer::convertHash(const Node& obj, int level) {
    std::vector<std::string> res;
    if ((obj.getFlags() & Tag::YAML_FLOW) == Tag::YAML_FLOW) { level = flowLevel; }
    for (auto item : obj.items()) {
        Node& val = item->value;
        if (val.getTag() == Tag::YAML_COMMENT) {
            res.push_back(convert(val, level+1));
        } else {
            if (skipValue(item->value)) { continue; }
            std::string key = convert(item->key, YAML_KEY_STRING_LEVEL);
            std::string valstr = convert(val, level+1);
            const char* sep = isspace(*valstr.c_str()) ? ":" : ": ";
            res.push_back(spacing(level) + key + sep + valstr);
        }
    }
    if (res.empty())
        return "{}";
    if (!indent || level >= flowLevel)
        return "{ " + strJoin(res, ", ") + " }";
    std::string block = strJoin(res, std::string(std::max(1, 1+extraLines - level), '\n'));
    return level > 0 ? ("\n" + block) : (block + "\n");
}

std::string Writer::convert(const Node& obj, int level) {
    switch (obj.getTag()) {
    case Tag::ARRAY:
        return convertArray(obj, level);
    case Tag::OBJECT:
        return convertHash(obj, level);
    case Tag::STRING:
        if (!indent) { return escapeDoubleQuoted(obj.getString()); }  // JSON
        switch(obj.getFlags() & Tag::YAML_STRINGMASK) {
        case Tag::YAML_SINGLEQUOTED:
            return escapeSingleQuoted(obj.getString());
        case Tag::YAML_UNQUOTED: {
            const std::string& str = obj.getString();
            const char* special = level < flowLevel ? "#\r\n" : ",:]}#\r\n";
            if (str.empty() || isspace(str[0]) || str[0] == '"' ||
                    str[0] == '\'' || str.find_first_of(special) != std::string::npos) {
                return escapeQuoted(str, quote);
            }
            if (level == YAML_KEY_STRING_LEVEL) {
                static std::string keyspecial("!&*-:?{}[],#|>@`\"'%");
                if (keyspecial.find_first_of(str[0]) != std::string::npos
                        || str.find_first_of(":#") != std::string::npos) {
                    return escapeQuoted(str, quote);
                }
            }
            if (!(obj.getFlags() & Tag::PARSED) && (str[0] == '-' || isdigit(str[0]))) {
                return escapeQuoted(str, quote);
            }
            return str;  // actually unquoted!
        }
        case Tag::YAML_BLOCKSTRING:
            // block string node could have been moved/copied or flow level could be different from input file
            if (level < flowLevel)
                return blockString(obj.getString(), spacing(level));
            [[fallthrough]];
        case Tag::YAML_DBLQUOTED:
        default:
            return escapeDoubleQuoted(obj.getString());
        }
    case Tag::NUMBER:
    {
        double val = obj.getNumber();
        return int64_t(val) == val ? std::to_string(int64_t(val)) : double2string(val);
    }
    case Tag::JSON_NULL:
        return "null";
    case Tag::JSON_BOOL:
        return obj.as<std::string>("false");  //obj.getNumber() != 0 ? "true" : "false";
    case Tag::YAML_COMMENT:
        return indent ? " #" + obj.getString() + "\n" : "";
    default:
        return "";
    }
}

std::string Dump(const Node& node) {
    YAML::Writer writer;
    writer.indent = 4;
    //writer.extraLines = 1;
    return writer.convert(node);
}

}  // namespace YAML

#ifndef GAML_LIB_ONLY

YAML::Node basicTests()
{
  static const char* yaml = R"(# comment
layer1:
  "sub1": 4
  'sub2': 'hello'
  sub3: {a: 5, b: "test"}
  empty_at_end:
empty_layer:
layer2:
    -   item1
    -          # empty array item
    -   "item2"
    -   - nested array
        - second item
    -   objinarray: val1
        empty_map: {}
        key2: val2
        emptyatend:
)";

  static const char* json = R"({
  "json1": {"sub1": 4, "sub2": "hello"},
  "json2": ["item1", "item2"]
})";

  static const char* colonspace = R"END(
    import: imports/urls.yaml
    fonts: { fontA: { url: https://host/font.woff } }
    sources: { sourceA: { url: 'https://host/tiles/{z}/{y}/{x}.mvt' } }
    textures:
        tex1: { url: path/to/texture.png#not-a-comment, something: else }
        tex2: { url: "../up_a_directory.png" }
    styles:
        styleA:
            texture: https://host/font.woff#this-is-not-a-comment  #but this is
            need:spaceto: make-a-key
            shaders:
                uniforms:
                    u_tex1: "/at_root.png"
                    u_tex2: ["path/to/texture.png", tex2]
                    u_tex3: tex3
                    u_bool: true
                    u_float: 0.25
)END";

  YAML::Node doc = YAML::parse(yaml);

  doc.merge({
    {"layer1", {
      {"sub3", {
          {"c", "merged"}
      }}
    }},
    {"a", { {"b", "this is a.b"} }},
    {"b", 4.6},
    {"z", "true"},
    {"empty", YAML::Map()}
  });

  std::string teststr = "teststr";
  doc.add("more") = {
    { "level1_1", 4 },
    { "level1_2", 1.45435515E-45 },
    { "level2", {
        { "level2_1", teststr },
        { "level2_2", "5.5" },
    }},
  };

  doc["a"]["c"] = YAML::Array({"this is a.c[0]", "this is a.c[1]", "this is a.c[2]"});
  doc["a"]["d"] = YAML::Array({ { {"a", 5}, {"b", "xxx"} }, "this is a.c[0]"});

  doc["a"]["c"].push_back("this is a.c[3]");
  doc["b"] = 5.6;
  doc["cloned"] = doc["more"].clone();
  doc.add("c") = YAML::Node({{"x", "this is c.x"}, {"y", "this is c.y"}, {"z", 4.5}});
  doc["c"]["a"]["b"] = "create nested with # symbol";

  const YAML::Node& nodec = doc["c"];
  nodec["m"]["n"].as<std::string>("const Node doesn't create anything");

  YAML::Node jdoc = YAML::parse(json);
  //doc.add("jdoc") = std::move(*jdoc.value);
  doc.add("jdoc") = std::move(jdoc);

  doc["colonspace"] = YAML::parse(colonspace);

  doc["single_scalar"] = YAML::parse("test_value");
  doc["single_array"] = YAML::parse("- test_item");
  doc["single_obj"] = YAML::parse("test_key:");

  assert(doc["a"]["b"].Scalar() == "this is a.b");
  assert(doc["b"].as<double>() == 5.6);

  assert(doc["b"].as<int>() == 5);
  assert(doc["b"].as<size_t>() == 5);
  assert(doc["b"].as<float>() == 5.6f);

  doc.remove("b");
  assert(doc["b"].as<double>(0) == 0);

  assert(doc["z"].as<bool>(false) == true);

  doc["layer2"][3].insert(0, "first item");
  doc["layer2"][3].remove(1);
  doc["layer2"][3].push_back("fourth item");
  doc["layer2"][3].insert(2, "third item");

  YAML::Writer writer;
  writer.indent = 4;
  writer.extraLines = 1;
  std::string out = writer.convert(doc);
  puts(out.c_str());
  fflush(stdout);

  return doc;
}

int main(int argc, char **argv) {

    if (argc < 2) {
        fprintf(stderr, "No input file specified!\n");
        YAML::Node doc = basicTests();
        return -1;
    }

    std::string infile(argv[1]);
    std::ifstream instrm(infile);  //"stylus-osm.yaml"); //argv[1]);
    std::stringstream buffer;
    buffer << instrm.rdbuf();

    //size_t ext = in.find_last_of('.');
    //std::ofstream outstr(infile.substr(0, ext) + );
    bool isJson = infile.substr(infile.size() - 5) == ".json";

    YAML::Node doc = YAML::parse(buffer.str(), isJson ? YAML::PARSE_JSON : 0);

    YAML::Writer writer;
    writer.indent = isJson ? 0 : 4;
    writer.extraLines = 1;
    std::string out = writer.convert(doc);
    puts(out.c_str());
    return 0;
}

#endif
