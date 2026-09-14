#pragma once

/**
 * spell_description_eval.hpp - the little expression language spell
 * descriptions are written in.
 *
 * A description can say "$<percent>% of your normal weapon damage", and what
 * percent is depends on the spell. SpellDescriptionVariables.dbc holds one row
 * per spell that needs it, declaring names in terms of the player. Sinister
 * Strike's row reads
 *
 *     $opportunity1=$?s14057[${110}][${100}]
 *     $percent=$?s14072[${120}][${$<opportunity1>}]
 *
 * - a hundred, or more with the Opportunity talent. Nothing read that table,
 * and the formatter had no branch for "$<": it dropped those two characters as
 * an unknown token and left the rest of the name in the sentence, so the
 * tooltip read "percent>% of your normal weapon damage".
 *
 * Three constructs, and only these three:
 *
 *     ${ arithmetic }         + - * / and parentheses
 *     $?s<id>[then][else]     whether the player knows that spell
 *     $<name>                 another declaration in the same row
 *
 * with $m1 $M1 $s1 $o1 for the spell's own magnitudes. Anything else fails the
 * whole expression rather than guessing at it, and the caller drops a token it
 * could not work out rather than leaking the name into the sentence.
 *
 * Free of the game handler so it can be tested: what a magnitude is and which
 * spells are known are asked of the caller.
 */

#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>

namespace wowee {
namespace game {

/// What the expression may ask about the world around it.
struct SpellDescriptionContext {
    /// $m1 / $M1 / $s1 / $o1 - a spell effect's magnitude, by zero-based
    /// index. False when there is none, which fails the expression.
    std::function<bool(int index, double& out)> magnitude;
    /// Whether the player knows a spell, for $?s<id>[then][else].
    std::function<bool(uint32_t spellId)> knowsSpell;
    /// The "$name=expression" lines this spell draws on, or null.
    const std::string* declarations = nullptr;
};

/// Evaluate one expression - a whole declaration, or the body of a ${}.
/// False when any part of it is something this does not model.
bool evaluateSpellExpression(const std::string& text,
                             const SpellDescriptionContext& ctx,
                             double& out);

/// The unevaluated right-hand side of "$name=" in ctx.declarations.
bool spellDeclarationOf(const SpellDescriptionContext& ctx,
                        const std::string& name, std::string& out);

}  // namespace game
}  // namespace wowee
