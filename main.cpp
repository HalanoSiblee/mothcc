#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cctype>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <climits>
#include <cstdlib>
#ifdef __linux__
#include <sys/xattr.h>
#endif
enum class LineEnding {
    LF, CRLF
};
static LineEnding detect_line_ending(const std::string & s) {
    size_t crlf = 0, lf = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n') {
            if (i > 0 && s[i - 1] == '\r')++crlf;
            else++lf;
        }
    }
    return (crlf > lf) ? LineEnding::CRLF : LineEnding::LF;
}

static void apply_line_ending(std::string & s, LineEnding le) {
    if (le == LineEnding::LF) {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n')continue;
            out += s[i];
        }
        s.swap(out);
        return;
    }
    std::string out;
    out.reserve(s.size() + s.size() / 20 + 16);
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n' && (i == 0 || s[i - 1] != '\r'))out += '\r';
        out += s[i];
    }
    s.swap(out);
}

static bool stat_changed(const struct stat & a, const struct stat & b) {
#if defined(__APPLE__)
    return a.st_mtimespec.tv_sec != b.st_mtimespec.tv_sec || a.st_mtimespec.tv_nsec != b.st_mtimespec.tv_nsec || a.st_size != b.st_size;
#else
    return a.st_mtim.tv_sec != b.st_mtim.tv_sec || a.st_mtim.tv_nsec != b.st_mtim.tv_nsec || a.st_size != b.st_size;
#endif
}

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
                if (src[i] == '\n' || src[i] == '\r')break;
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

static bool overwrite_file(const std::string & path, const std::string & data, const struct stat * expected_stat) {
    std::string real_path = path;
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved))real_path = resolved;
    struct stat st {
    };
    bool have_stat = (stat(real_path.c_str(), &st) == 0);
    if (have_stat && !S_ISREG(st.st_mode)) {
        std::cerr << "nuke: not a regular file: " << real_path << "\n";
        return false;
    }
    std::string tmpl = real_path + ".nuke.XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    int fd = mkstemp(buf.data());
    if (fd < 0) {
        std::cerr << "nuke: mkstemp(" << real_path << "): " << std::strerror(errno) << "\n";
        return false;
    }
    std::string tmp_path(buf.data());
    const char *p = data.data();
    size_t remaining = data.size();
    while (remaining > 0) {
        ssize_t n = write(fd, p, remaining);
        if (n < 0) {
            if (errno == EINTR)continue;
            std::cerr << "nuke: write(" << tmp_path << "): " << std::strerror(errno) << "\n";
            close(fd);
            unlink(tmp_path.c_str());
            return false;
        }
        p += n;
        remaining -= (size_t)n;
    }
    if (have_stat) {
        fchmod(fd, st.st_mode & 07777);
        fchown(fd, st.st_uid, st.st_gid);
#ifdef __linux__
        ssize_t listlen = listxattr(real_path.c_str(), nullptr, 0);
        if (listlen > 0) {
            std::vector<char> names((size_t)listlen);
            if (listxattr(real_path.c_str(), names.data(), (size_t)listlen) > 0) {
                char *q = names.data();
                char *end = names.data() + listlen;
                while (q < end) {
                    std::string name(q);
                    q += name.size() + 1;
                    ssize_t vlen = getxattr(real_path.c_str(), name.c_str(), nullptr, 0);
                    if (vlen > 0) {
                        std::vector<char> val((size_t)vlen);
                        if (getxattr(real_path.c_str(), name.c_str(), val.data(), (size_t)vlen) == vlen)fsetxattr(fd, name.c_str(), val.data(), (size_t)vlen, 0);
                    }
                }
            }
        }
#endif
    }
    if (fsync(fd) < 0) {
        std::cerr << "nuke: warning: fsync(" << tmp_path << "): " << std::strerror(errno) << "\n";
    }
    if (close(fd) < 0) {
        std::cerr << "nuke: close(" << tmp_path << "): " << std::strerror(errno) << "\n";
        unlink(tmp_path.c_str());
        return false;
    }
    if (expected_stat) {
        struct stat now {
        };
        if (stat(real_path.c_str(), &now) == 0 && stat_changed( *expected_stat, now)) {
            std::cerr << "nuke: " << real_path << " changed during processing, aborting\n";
            unlink(tmp_path.c_str());
            return false;
        }
    }
    if (rename(tmp_path.c_str(), real_path.c_str()) < 0) {
        std::cerr << "nuke: rename(" << tmp_path << " -> " << real_path << "): " << std::strerror(errno) << "\n";
        unlink(tmp_path.c_str());
        return false;
    }
    return true;
}

static void usage(const char *prog) {
    std::cout << "usage: " << prog << " [options] [input-file]\n" "\n" "Modes (default: clean + prettify):\n" "  -c, --clean         strip comments only\n" "  -p, --pretty        reformat only (input must be comment-free)\n" "\n" "Output:\n" "  -o FILE             write to FILE (default: stdout)\n" "  -ov, --overwrite    rewrite input file in place (atomic)\n" "  -i                  alias for -ov (sed -i style)\n" "  -b, --backup        with -ov: save original to FILE.bak\n" "  --check             exit 1 if file would change, 0 if clean\n" "\n" "Other:\n" "  -h, --help          show this help\n" "\n" "Input '-' or omitted reads from stdin.\n" "\n" "Examples:\n" "  " << prog << " foo.cpp > foo.clean.cpp\n" "  " << prog << " -c foo.c                  # comments only, to stdout\n" "  " << prog << " -i src/*.cpp              # rewrite in place, keep mtime if no-op\n" "  " << prog << " -i -b src/foo.cpp         # ...and save foo.cpp.bak first\n" "  " << prog << " --check src/*.cpp         # CI: fail if any file needs cleaning\n";
}

int main(int argc, char * *argv) {
    bool cleanOnly = false;
    bool prettyOnly = false;
    bool overwrite = false;
    bool backup = false;
    bool check = false;
    std::string infile, outfile;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-c" || a == "--clean")cleanOnly = true;
        else if (a == "-p" || a == "--pretty")prettyOnly = true;
        else if (a == "-o" && i + 1 < argc)outfile = argv[ ++i];
        else if (a == "-ov" || a == "--overwrite")overwrite = true;
        else if (a == "-i")overwrite = true;
        else if (a == "-b" || a == "--backup")backup = true;
        else if (a == "--check")check = true;
        else if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return 0;
        } else if (a.size() > 1 && a[0] == '-') {
            std::cerr << "unknown option: " << a << "\n";
            return 1;
        } else infile = a;
    }
    if (overwrite && !outfile.empty()) {
        std::cerr << "nuke: -ov and -o are mutually exclusive\n";
        return 1;
    }
    if (overwrite && (infile.empty() || infile == "-")) {
        std::cerr << "nuke: -ov requires a file path, not stdin\n";
        return 1;
    }
    if (backup && !overwrite) {
        std::cerr << "nuke: -b requires -ov\n";
        return 1;
    }
    if (check && (overwrite || !outfile.empty() || backup)) {
        std::cerr << "nuke: --check cannot be combined with -ov/-o/-b\n";
        return 1;
    }
    if (check && (infile.empty() || infile == "-")) {
        std::cerr << "nuke: --check requires a file path, not stdin\n";
        return 1;
    }
    struct stat src_stat_before {
    };
    bool have_stat_before = false;
    if (!infile.empty() && infile != "-") {
        if (stat(infile.c_str(), &src_stat_before) == 0)have_stat_before = true;
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
    LineEnding src_le = detect_line_ending(src);
    bool had_trailing_nl = !src.empty() && src.back() == '\n';
    bool ran_prettier = !cleanOnly;
    std::string result;
    if (cleanOnly)result = cleancc(src);
    else if (prettyOnly)result = prettiercc(src);
    else result = prettiercc(cleancc(src));
    if (ran_prettier && !had_trailing_nl) {
        if (!result.empty() && result.back() == '\n')result.pop_back();
    }
    apply_line_ending(result, src_le);
    if (check) {
        if (result == src)return 0;
        std::cerr << "nuke: " << infile << " would be modified\n";
        return 1;
    }
    if (overwrite) {
        if (result == src)return 0;
        if (backup) {
            std::string bak = infile + ".bak";
            std::ofstream bf(bak, std::ios::binary | std::ios::trunc);
            if (!bf) {
                std::cerr << "nuke: cannot create " << bak << "\n";
                return 1;
            }
            bf.write(src.data(), (std::streamsize)src.size());
            if (!bf) {
                std::cerr << "nuke: write failed for " << bak << "\n";
                return 1;
            }
            std::cerr << "nuke: backed up " << infile << " -> " << bak << "\n";
        }
        if (!overwrite_file(infile, result, have_stat_before ? &src_stat_before : nullptr))return 1;
        std::cerr << "nuke: rewrote " << infile << "\n";
        return 0;
    }
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