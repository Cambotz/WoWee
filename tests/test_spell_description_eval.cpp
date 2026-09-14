// The little expression language spell descriptions are written in.
//
// A description can say "$<percent>% of your normal weapon damage", and what
// percent is depends on the spell. Nothing read SpellDescriptionVariables.dbc,
// and the formatter had no branch for "$<" - it dropped those two characters
// as an unknown token and left the rest of the name in the sentence, so
// Sinister Strike's tooltip read "percent>% of your normal weapon damage".

#include <catch2/catch_amalgamated.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "game/spell_description_eval.hpp"

using namespace wowee::game;

namespace {

/// Sinister Strike's row of SpellDescriptionVariables.dbc, verbatim.
const std::string kSinisterStrike =
    "$opportunity1=$?s14057[${110}][${100}]\r\n"
    "$percent=$?s14072[${120}][${$<opportunity1>}]";

SpellDescriptionContext contextFor(const std::string& declarations,
                                   std::vector<uint32_t> known = {},
                                   std::vector<double> magnitudes = {}) {
    static std::string decls;
    static std::vector<uint32_t> knownSpells;
    static std::vector<double> mags;
    decls = declarations;
    knownSpells = std::move(known);
    mags = std::move(magnitudes);

    SpellDescriptionContext ctx;
    ctx.declarations = &decls;
    ctx.knowsSpell = [](uint32_t id) {
        return std::find(knownSpells.begin(), knownSpells.end(), id) != knownSpells.end();
    };
    ctx.magnitude = [](int index, double& out) {
        if (index < 0 || index >= static_cast<int>(mags.size())) return false;
        out = mags[static_cast<std::size_t>(index)];
        return true;
    };
    return ctx;
}

double evaluated(const std::string& name, const SpellDescriptionContext& ctx) {
    std::string decl;
    REQUIRE(spellDeclarationOf(ctx, name, decl));
    double out = 0.0;
    REQUIRE(evaluateSpellExpression(decl, ctx, out));
    return out;
}

}  // namespace

TEST_CASE("a rogue with no talents strikes for the base percentage",
          "[spell][description]") {
    const auto ctx = contextFor(kSinisterStrike);
    // Neither Opportunity rank is known, so both conditions take their else
    // branch and the answer is the hundred the tooltip should have shown.
    CHECK(evaluated("percent", ctx) == Catch::Approx(100.0));
}

TEST_CASE("a talent the player has changes the number", "[spell][description]") {
    CHECK(evaluated("percent", contextFor(kSinisterStrike, {14057})) == Catch::Approx(110.0));
    CHECK(evaluated("percent", contextFor(kSinisterStrike, {14072})) == Catch::Approx(120.0));
    // The second rank wins: its branch does not consult the first.
    CHECK(evaluated("percent", contextFor(kSinisterStrike, {14057, 14072})) == Catch::Approx(120.0));
}

TEST_CASE("a declaration can name another one", "[spell][description]") {
    // $percent's else branch is ${$<opportunity1>}, so resolving it at all
    // means the nested lookup worked.
    const auto ctx = contextFor(kSinisterStrike, {14057});
    CHECK(evaluated("opportunity1", ctx) == Catch::Approx(110.0));
}

TEST_CASE("arithmetic, in the order it is written", "[spell][description]") {
    const auto ctx = contextFor("$a=${2+3*4}\r\n$b=${(2+3)*4}\r\n$c=${10/4}");
    // Left to right, which is what these expressions are written for - the
    // parenthesised form is how the data groups when it means to.
    CHECK(evaluated("a", ctx) == Catch::Approx(20.0));
    CHECK(evaluated("b", ctx) == Catch::Approx(20.0));
    CHECK(evaluated("c", ctx) == Catch::Approx(2.5));
}

TEST_CASE("a spell's own magnitudes are asked of the caller", "[spell][description]") {
    const auto ctx = contextFor("$dmg=${$m1*2}", {}, {7.0, 11.0});
    CHECK(evaluated("dmg", ctx) == Catch::Approx(14.0));
}

TEST_CASE("a quantity this client does not keep fails the whole expression",
          "[spell][description]") {
    // $AP is attack power, which is not modelled here. Standing a zero in for
    // it would put a wrong number in a tooltip, which is worse than no number.
    const auto ctx = contextFor("$x=${$m1+$AP*0.03}", {}, {5.0});
    std::string decl;
    REQUIRE(spellDeclarationOf(ctx, "x", decl));
    double out = 0.0;
    CHECK_FALSE(evaluateSpellExpression(decl, ctx, out));
}

TEST_CASE("a name that is not declared is not a number", "[spell][description]") {
    const auto ctx = contextFor(kSinisterStrike);
    std::string decl;
    CHECK_FALSE(spellDeclarationOf(ctx, "nosuchthing", decl));
    double out = 0.0;
    CHECK_FALSE(evaluateSpellExpression("$<nosuchthing>", ctx, out));
}

TEST_CASE("a declaration naming itself stops rather than recurring forever",
          "[spell][description]") {
    const auto ctx = contextFor("$loop=${$<loop>+1}");
    double out = 0.0;
    CHECK_FALSE(evaluateSpellExpression("$<loop>", ctx, out));
}

TEST_CASE("malformed expressions are refused, not guessed at",
          "[spell][description]") {
    const auto ctx = contextFor("$x=1");
    double out = 0.0;
    CHECK_FALSE(evaluateSpellExpression("${1+}", ctx, out));
    CHECK_FALSE(evaluateSpellExpression("${(1+2}", ctx, out));
    CHECK_FALSE(evaluateSpellExpression("$?s123[10]", ctx, out));   // one branch
    CHECK_FALSE(evaluateSpellExpression("${1/0}", ctx, out));
    CHECK_FALSE(evaluateSpellExpression("", ctx, out));
}
