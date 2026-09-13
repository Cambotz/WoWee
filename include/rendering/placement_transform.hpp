#pragma once

/// The model matrix a placement's position, rotation and scale become.
///
/// MDDF places doodads and MODF places buildings, and both store the same three
/// degrees of rotation the same way. With no pitch and no roll every
/// composition order is the same rotation, so an upright tree or a building on
/// flat ground looks correct whichever order built it. That is what made this
/// so hard to settle by eye: five attempts were each judged against evidence
/// that could not tell them apart, and the order was wrong for all of them.
///
/// It is not judged by eye any more. MODF stores, beside each placement, the
/// world-space bounding box Blizzard's own tools computed for it - so the
/// convention can be solved rather than guessed. Transforming each WMO's root
/// bounding box by a candidate and comparing against the stored one, over the
/// 1469 placements in 3.3.5's ADTs that have more than three degrees of pitch
/// or roll, separates the candidates completely: one reproduces the stored
/// bounds to zero error on every single one, and the next best is out by a
/// median of 4.3 yards and as much as 120.
///
/// The answer is Z, then Y, then X, over an euler triple of
/// (rot[2], rot[0], rot[1] + 180) - see placementEuler in terrain_manager.cpp.
/// Both halves matter and neither works alone, which is why trying the six
/// orders and the sign of each component separately never landed on it: the
/// order that is right needs the signs that were wrong, and the other way
/// round. Composing X, Y, Z with the signs negated - what this did before - put
/// Silverpine's chasm bridge across its ravine at a visible slant.
///
/// tests/test_placement_transform.cpp holds four of those measured placements
/// and checks the bounds this reproduces against the ones on disk, so the next
/// change to the order answers to the data rather than to a screenshot.

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace wowee::rendering {

/// Translate, then rotate Z, Y, X, then scale uniformly.
///
/// `eulerRadians` is the placement's rotation already mapped into render axes.
/// The order is the whole point of this living in one place; see above.
inline glm::mat4 placementModelMatrix(const glm::vec3& position,
                                      const glm::vec3& eulerRadians,
                                      float scale) {
    glm::mat4 m(1.0f);
    m = glm::translate(m, position);
    m = glm::rotate(m, eulerRadians.z, glm::vec3(0.0f, 0.0f, 1.0f));
    m = glm::rotate(m, eulerRadians.y, glm::vec3(0.0f, 1.0f, 0.0f));
    m = glm::rotate(m, eulerRadians.x, glm::vec3(1.0f, 0.0f, 0.0f));
    m = glm::scale(m, glm::vec3(scale));
    return m;
}

}  // namespace wowee::rendering
