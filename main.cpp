/*
g++ -std=c++17 -O3 -march=native -flto -fno-plt -fno-exceptions \
    -fno-rtti -DNDEBUG -s -o mothcc main.cpp
*/
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cctype>
std::string cleancc(const std::string & src) {
    std::string out;
    out.reserve(src.size());
    size_t i = 0, n = src.size();
    while (i < n) {
        char c = src[i];
        char d = (i + 1 < n) ? src[i + 1] : '\0';
        if (c == 'R' && d == '"') {
            size_t j = i + 2;
            std::string delim;
            while (j < n && src[j] != '(' && src[j] != '"' && !std::isspace((unsigned char)src[j]))delim += src[j++];
            if (j < n && src[j] == '(') {
                std::string term = ")" + delim + "\"";
                size_t e = src.find(term, j + 1);
                size_t end = (e == std::string::npos) ? n : e + term.size();
                out.append(src, i, end - i);
                i = end;
                continue;
            }
        }
        if (c == '/' && d == '/') {
            i += 2;
            while (i < n) {
                if (src[i] == '\\' && i + 1 < n && src[i + 1] == '\n') {
                    i += 2;
                    continue;
                }
                if (src[i] == '\n')break;
                ++i;
            }
            continue;
        }
        if (c == '/' && d == '*') {
            if (!out.empty() && !std::isspace((unsigned char)out.back()))out += ' ';
            i += 2;
            while (i + 1 < n && !(src[i] == '*' && src[i + 1] == '/'))++i;
            i = (i + 1 < n) ? i + 2 : n;
            continue;
        }
        if (c == '"' || c == '\'') {
            char q = c;
            out += c;
            ++i;
            while (i < n) {
                if (src[i] == '\\' && i + 1 < n) {
                    out += src[i];
                    out += src[i + 1];
                    i += 2;
                    continue;
                }
                out += src[i];
                if (src[i] == q) {
                    ++i;
                    break;
                }
                if (src[i] == '\n') {
                    ++i;
                    break;
                }
                ++i;
            }
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

enum Kind {
    K_IDENT, K_NUM, K_STR, K_CHR, K_PUNCT, K_PREPROC
};
struct Tok {
    std::string text;
    Kind kind;
    bool wsBefore;
};
static std::vector<Tok> tokenize(const std::string & s) {
    static const std::vector<std::string> OPS = {
        ">>=", "<<=", "->*", "...", ">>", "<<", "<=", ">=", "==", "!=", "&&", "||", "++", "--", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "->", "::", ".*", "##"
    };
    std::vector<Tok> toks;
    size_t i = 0, n = s.size();
    bool atLineBegin = true;
    bool ws = false;
    while (i < n) {
        char c = s[i];
        if (c == '\n') {
            atLineBegin = true;
            ws = true;
            ++i;
            continue;
        }
        if (std::isspace((unsigned char)c)) {
            ws = true;
            ++i;
            continue;
        }
        if (c == '#' && atLineBegin) {
            size_t j = i;
            std::string line;
            while (j < n) {
                if (s[j] == '\\' && j + 1 < n && s[j + 1] == '\n') {
                    line += s[j];
                    line += s[j + 1];
                    j += 2;
                    continue;
                }
                if (s[j] == '\n')break;
                line += s[j];
                ++j;
            } while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))line.pop_back();
            toks.push_back( {
                line, K_PREPROC, true
            });
            i = j;
            ws = false;
            continue;
        }
        atLineBegin = false;
        if (std::isalpha((unsigned char)c) || c == '_') {
            if (c == 'R' && i + 1 < n && s[i + 1] == '"') {
                size_t k = i + 2;
                std::string delim;
                while (k < n && s[k] != '(' && s[k] != '"' && !std::isspace((unsigned char)s[k]))delim += s[k++];
                if (k < n && s[k] == '(') {
                    std::string term = ")" + delim + "\"";
                    size_t e = s.find(term, k + 1);
                    size_t end = (e == std::string::npos) ? n : e + term.size();
                    toks.push_back( {
                        s.substr(i, end - i), K_STR, ws
                    });
                    i = end;
                    ws = false;
                    continue;
                }
            }
            if ((c == 'L' || c == 'u' || c == 'U') && i + 1 < n) {
                size_t p = i + 1;
                if (c == 'u' && p < n && s[p] == '8')++p;
                if (p < n && (s[p] == '"' || s[p] == '\'')) {
                    char q = s[p];
                    size_t k = p + 1;
                    while (k < n) {
                        if (s[k] == '\\' && k + 1 < n) {
                            k += 2;
                            continue;
                        }
                        if (s[k] == q) {
                            ++k;
                            break;
                        }
                        if (s[k] == '\n') {
                            ++k;
                            break;
                        }
                        ++k;
                    }
                    toks.push_back( {
                        s.substr(i, k - i), q == '"' ? K_STR : K_CHR, ws
                    });
                    i = k;
                    ws = false;
                    continue;
                }
            }
            size_t j = i;
            while (j < n && (std::isalnum((unsigned char)s[j]) || s[j] == '_'))++j;
            toks.push_back( {
                s.substr(i, j - i), K_IDENT, ws
            });
            i = j;
            ws = false;
            continue;
        }
        if (std::isdigit((unsigned char)c) || (c == '.' && i + 1 < n && std::isdigit((unsigned char)s[i + 1]))) {
            size_t j = i;
            while (j < n) {
                char ch = s[j];
                if (std::isalnum((unsigned char)ch) || ch == '_' || ch == '.') {
                    ++j;
                    continue;
                }
                if ((ch == '+' || ch == '-') && j > i && (s[j - 1] == 'e' || s[j - 1] == 'E' || s[j - 1] == 'p' || s[j - 1] == 'P')) {
                    ++j;
                    continue;
                }
                break;
            }
            toks.push_back( {
                s.substr(i, j - i), K_NUM, ws
            });
            i = j;
            ws = false;
            continue;
        }
        if (c == '"' || c == '\'') {
            char q = c;
            size_t j = i + 1;
            while (j < n) {
                if (s[j] == '\\' && j + 1 < n) {
                    j += 2;
                    continue;
                }
                if (s[j] == q) {
                    ++j;
                    break;
                }
                if (s[j] == '\n') {
                    ++j;
                    break;
                }
                ++j;
            }
            toks.push_back( {
                s.substr(i, j - i), q == '"' ? K_STR : K_CHR, ws
            });
            i = j;
            ws = false;
            continue;
        }
        {
            bool matched = false;
            for (const auto &op : OPS) {
                if (i + op.size() <= n && s.compare(i, op.size(), op) == 0) {
                    toks.push_back( {
                        op, K_PUNCT, ws
                    });
                    i += op.size();
                    ws = false;
                    matched = true;
                    break;
                }
            }
            if (matched)continue;
            toks.push_back( {
                std::string(1, c), K_PUNCT, ws
            });
            ++i;
            ws = false;
        }
    }
    return toks;
}

static bool isParenKeyword(const std::string & s) {
    return s == "if" || s == "for" || s == "while" || s == "switch" || s == "catch" || s == "return" || s == "new" || s == "delete" || s == "throw";
}

static bool isTypeKeyword(const std::string & s) {
    static const char *kw[] = {
        "void", "char", "short", "int", "long", "float", "double", "signed", "unsigned", "bool", "wchar_t", "char16_t", "char32_t", "auto", "const", "volatile", "struct", "class", "enum", "union", "typename", "size_t", "ssize_t", "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "FILE"
    };
    for (auto k : kw)if (s == k)return true;
    return false;
}

std::string prettiercc(const std::string & src) {
    std::vector<Tok> toks = tokenize(src);
    std::string out;
    out.reserve(src.size() + src.size() / 2 + 64);
    int indent = 0;
    int parenDepth = 0;
    int angleDepth = 0;
    int ternaryDepth = 0;
    bool atLineStart = true;
    bool inCaseLabel = false;
    bool extraCaseIndent = false;
    auto trimTrailing = [ & ] {
        while (!out.empty() && (out.back() == ' ' || out.back() == '\t'))out.pop_back();
    };
    auto newline = [ & ] {
        if (atLineStart)return;
        trimTrailing();
        out += '\n';
        atLineStart = true;
    };
    auto emit = [ & ](const std::string & s) {
        if (atLineStart) {
            out.append((size_t)indent * 4, ' ');
            atLineStart = false;
        }
        out += s;
    };
    auto space = [ & ] {
        if (atLineStart)return;
        if (!out.empty() && out.back() != ' ' && out.back() != '\n')out += ' ';
    };
    const size_t N = toks.size();
    for (size_t i = 0; i < N; ++i) {
        const Tok & t = toks[i];
        const Tok * prev = (i > 0) ? &toks[i - 1] : nullptr;
        const Tok * next = (i + 1 < N) ? &toks[i + 1] : nullptr;
        if (t.kind == K_PREPROC) {
            newline();
            int saved = indent;
            indent = 0;
            emit(t.text);
            indent = saved;
            newline();
            continue;
        }
        if (t.kind == K_IDENT && (t.text == "case" || t.text == "default")) {
            if (extraCaseIndent) {
                --indent;
                extraCaseIndent = false;
            }
            newline();
            emit(t.text);
            inCaseLabel = true;
            continue;
        }
        if (t.kind == K_PUNCT) {
            const std::string & p = t.text;
            if (p == "{") {
                space();
                emit("{");
                ++indent;
                newline();
            } else if (p == "}") {
                if (extraCaseIndent) {
                    --indent;
                    extraCaseIndent = false;
                }
                --indent;
                if (indent < 0)indent = 0;
                newline();
                emit("}");
                bool attach = false;
                if (next) {
                    if (next->kind == K_PUNCT && (next->text == ";" || next->text == "," || next->text == ")"))attach = true;
                    if (next->kind == K_IDENT && (next->text == "else" || next->text == "catch" || next->text == "while"))attach = true;
                }
                if (attach) {
                    if (next->kind == K_IDENT)space();
                } else {
                    newline();
                    if (indent == 0)out += '\n';
                }
            } else if (p == "(") {
                if (prev && prev->kind == K_IDENT && isParenKeyword(prev->text))space();
                emit("(");
                ++parenDepth;
            } else if (p == ")") {
                if (parenDepth > 0)--parenDepth;
                emit(")");
            } else if (p == "[") {
                emit("[");
            } else if (p == "]") {
                emit("]");
            } else if (p == ";") {
                emit(";");
                if (parenDepth > 0)space();
                else newline();
            } else if (p == ",") {
                emit(",");
                space();
            } else if (p == ":") {
                if (inCaseLabel) {
                    emit(":");
                    newline();
                    ++indent;
                    extraCaseIndent = true;
                    inCaseLabel = false;
                } else if (prev && prev->kind == K_IDENT && (prev->text == "public" || prev->text == "private" || prev->text == "protected")) {
                    emit(":");
                    newline();
                } else {
                    if (ternaryDepth > 0)--ternaryDepth;
                    space();
                    emit(":");
                    space();
                }
            } else if (p == "?") {
                ++ternaryDepth;
                space();
                emit("?");
                space();
            } else if (p == "." || p == "->" || p == "::") {
                emit(p);
            } else if (p == "++" || p == "--") {
                bool postfix = prev && (prev->kind == K_IDENT || prev->kind == K_NUM || prev->text == ")" || prev->text == "]");
                if (postfix)emit(p);
                else {
                    space();
                    emit(p);
                }
            } else if (p == "!" || p == "~") {
                emit(p);
            } else if (p == "*" || p == "&") {
                bool unary = next && next->kind == K_IDENT && ((!prev) || (prev->kind == K_IDENT && isTypeKeyword(prev->text)) || (prev->kind == K_PUNCT && prev->text != ")" && prev->text != "]"));
                if (unary) {
                    space();
                    emit(p);
                } else {
                    space();
                    emit(p);
                    space();
                }
            } else if (p == "&&" || p == "||") {
                space();
                emit(p);
                space();
            } else if (p == "<") {
                bool tmpl = prev && prev->kind == K_IDENT && next && !t.wsBefore && !next->wsBefore && (next->kind == K_IDENT || next->kind == K_NUM || next->text == "::" || next->text == ">" || next->text == ",");
                if (tmpl) {
                    emit("<");
                    ++angleDepth;
                } else {
                    space();
                    emit("<");
                    space();
                }
            } else if (p == ">") {
                if (angleDepth > 0) {
                    --angleDepth;
                    emit(">");
                    if (angleDepth == 0 && next && (next->kind == K_IDENT || next->kind == K_NUM))space();
                } else {
                    space();
                    emit(">");
                    space();
                }
            } else if (p == ">>") {
                if (angleDepth >= 2) {
                    angleDepth -= 2;
                    emit(">>");
                    if (angleDepth == 0 && next && next->kind == K_IDENT)space();
                } else if (angleDepth == 1) {
                    angleDepth = 0;
                    emit(">>");
                    if (next && next->kind == K_IDENT)space();
                } else {
                    space();
                    emit(">>");
                    space();
                }
            } else if (p == "-" || p == "+") {
                bool unary = (!prev) || (prev->kind == K_PUNCT && prev->text != ")" && prev->text != "]") || (prev->kind == K_IDENT && (prev->text == "return" || prev->text == "case" || prev->text == "sizeof" || prev->text == "throw" || prev->text == "new" || prev->text == "delete"));
                if (unary) {
                    space();
                    emit(p);
                } else {
                    space();
                    emit(p);
                    space();
                }
            } else {
                space();
                emit(p);
                space();
            }
        } else {
            if (prev && (prev->kind == K_IDENT || prev->kind == K_NUM || prev->kind == K_STR || prev->kind == K_CHR))space();
            emit(t.text);
        }
    }
    newline();
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ' || out.back() == '\t'))out.pop_back();
    out += '\n';
    return out;
}

static void usage(const char *prog) {
    std::cout << "usage: " << prog << " [options] [input-file]\n" "  -c        clean comments only\n" "  -p        prettify only (input must already be comment-free)\n" "  -o FILE   write output to FILE (default stdout)\n" "  -h        show this help\n" "  input '-' or omitted => read from stdin\n" "Default: cleancc() then prettiercc().\n";
}

int main(int argc, char * *argv) {
    bool cleanOnly = false;
    bool prettyOnly = false;
    std::string infile, outfile;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-c" || a == "--clean")cleanOnly = true;
        else if (a == "-p" || a == "--pretty")prettyOnly = true;
        else if (a == "-o" && i + 1 < argc)outfile = argv[ ++i];
        else if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return 0;
        } else if (a.size() > 1 && a[0] == '-') {
            std::cerr << "unknown option: " << a << "\n";
            return 1;
        } else infile = a;
    }
    std::string src;
    if (infile.empty() || infile == "-") {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        src = ss.str();
    } else {
        std::ifstream f(infile, std::ios::binary);
        if (!f) {
            std::cerr << "cannot open: " << infile << "\n";
            return 1;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        src = ss.str();
    }
    std::string result;
    if (cleanOnly)result = cleancc(src);
    else if (prettyOnly)result = prettiercc(src);
    else result = prettiercc(cleancc(src));
    if (outfile.empty() || outfile == "-") {
        std::cout << result;
    } else {
        std::ofstream f(outfile, std::ios::binary);
        if (!f) {
            std::cerr << "cannot write: " << outfile << "\n";
            return 1;
        }
        f << result;
    }
    return 0;
}
