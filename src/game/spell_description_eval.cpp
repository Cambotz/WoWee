#include "game/spell_description_eval.hpp"

namespace wowee {
namespace game {
namespace {

struct Parser {
    const SpellDescriptionContext& ctx;
    int depth = 0;   ///< a row may name itself; stop rather than recurse forever

    bool value(const std::string& text, std::size_t& i, double& out);
    bool expression(const std::string& text, std::size_t& i, double& out);

    bool whole(const std::string& text, double& out) {
        std::size_t i = 0;
        if (!expression(text, i, out)) return false;
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        return i >= text.size();
    }
};

/// One term: a number, a parenthesised expression, or a $ token.
bool Parser::value(const std::string& text, std::size_t& i, double& out) {
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    if (i >= text.size()) return false;

    if (text[i] == '(') {
        ++i;
        if (!expression(text, i, out)) return false;
        if (i >= text.size() || text[i] != ')') return false;
        ++i;
        return true;
    }

    if (text[i] == '-') {
        ++i;
        double v = 0.0;
        if (!value(text, i, v)) return false;
        out = -v;
        return true;
    }

    if (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.') {
        std::size_t used = 0;
        try {
            out = std::stod(text.substr(i), &used);
        } catch (...) {
            return false;
        }
        i += used;
        return true;
    }

    if (text[i] != '$') return false;
    ++i;
    if (i >= text.size()) return false;

    // ${ arithmetic }
    if (text[i] == '{') {
        ++i;
        if (!expression(text, i, out)) return false;
        if (i >= text.size() || text[i] != '}') return false;
        ++i;
        return true;
    }

    // $<name>, another declaration in the same row
    if (text[i] == '<') {
        const std::size_t close = text.find('>', i + 1);
        if (close == std::string::npos) return false;
        const std::string name = text.substr(i + 1, close - i - 1);
        i = close + 1;
        if (depth > 8) return false;
        std::string decl;
        if (!spellDeclarationOf(ctx, name, decl)) return false;
        Parser nested{ctx, depth + 1};
        return nested.whole(decl, out);
    }

    // $?s<id>[then][else]
    if (text[i] == '?') {
        ++i;
        if (i >= text.size() || (text[i] != 's' && text[i] != 'S')) return false;
        ++i;
        uint32_t condId = 0;
        bool any = false;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
            condId = condId * 10 + static_cast<uint32_t>(text[i] - '0');
            any = true;
            ++i;
        }
        if (!any) return false;

        // Both branches are read whichever is taken, so a malformed one fails
        // the expression rather than passing unnoticed behind a condition that
        // happened not to choose it.
        auto bracketed = [&](std::string& body) -> bool {
            if (i >= text.size() || text[i] != '[') return false;
            int open = 0;
            const std::size_t start = ++i;
            for (; i < text.size(); ++i) {
                if (text[i] == '[') {
                    ++open;
                } else if (text[i] == ']') {
                    if (open == 0) {
                        body = text.substr(start, i - start);
                        ++i;
                        return true;
                    }
                    --open;
                }
            }
            return false;
        };
        std::string thenBody;
        std::string elseBody;
        if (!bracketed(thenBody)) return false;
        if (!bracketed(elseBody)) return false;
        if (depth > 8) return false;

        const bool known = ctx.knowsSpell && ctx.knowsSpell(condId);
        Parser nested{ctx, depth + 1};
        return nested.whole(known ? thenBody : elseBody, out);
    }

    // $m1 / $M1 / $s1 / $o1 - the spell's own magnitudes.
    const char code = text[i++];
    int index = 0;
    if (i < text.size() && text[i] >= '1' && text[i] <= '3') {
        index = text[i] - '1';
        ++i;
    }
    switch (code) {
        case 's': case 'S': case 'm': case 'M': case 'o': case 'O':
            return ctx.magnitude && ctx.magnitude(index, out);
        default:
            // $AP, $b1, $PL and the rest are quantities this client does not
            // keep. Refusing is better than standing in a number for them:
            // a tooltip with the wrong figure is worse than one without it.
            return false;
    }
}

bool Parser::expression(const std::string& text, std::size_t& i, double& out) {
    if (!value(text, i, out)) return false;
    while (true) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i >= text.size()) return true;
        const char op = text[i];
        if (op != '+' && op != '-' && op != '*' && op != '/') return true;
        ++i;
        double rhs = 0.0;
        if (!value(text, i, rhs)) return false;
        switch (op) {
            case '+': out += rhs; break;
            case '-': out -= rhs; break;
            case '*': out *= rhs; break;
            case '/':
                if (std::abs(rhs) < 1e-9) return false;
                out /= rhs;
                break;
            default: return false;
        }
    }
}

}  // namespace

bool spellDeclarationOf(const SpellDescriptionContext& ctx,
                        const std::string& name, std::string& out) {
    if (ctx.declarations == nullptr || name.empty()) return false;
    const std::string needle = "$" + name + "=";
    const std::size_t at = ctx.declarations->find(needle);
    if (at == std::string::npos) return false;
    const std::size_t from = at + needle.size();
    const std::size_t end = ctx.declarations->find_first_of("\r\n", from);
    out = ctx.declarations->substr(from,
                                   end == std::string::npos ? std::string::npos : end - from);
    return true;
}

bool evaluateSpellExpression(const std::string& text,
                             const SpellDescriptionContext& ctx, double& out) {
    if (text.empty()) return false;
    Parser parser{ctx, 0};
    return parser.whole(text, out);
}

}  // namespace game
}  // namespace wowee
