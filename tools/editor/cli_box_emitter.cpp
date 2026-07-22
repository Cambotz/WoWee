#include "cli_box_emitter.hpp"

#include <glm/glm.hpp>
#include <cstdint>

namespace wowee {
namespace editor {
namespace cli {

void addFlatBox(wowee::pipeline::WoweeModel& wom,
                float cx, float cy, float cz,
                float hx, float hy, float hz) {
    struct Face { glm::vec3 n, du, dv; };
    const Face faces[6] = {
        {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},   // top    (+Y)
        {{0,-1, 0}, {1, 0, 0}, {0, 0,-1}},   // bottom (-Y)
        {{1, 0, 0}, {0, 0, 1}, {0, 1, 0}},   // right  (+X)
        {{-1,0, 0}, {0, 0,-1}, {0, 1, 0}},   // left   (-X)
        {{0, 0, 1}, {-1,0, 0}, {0, 1, 0}},   // front  (+Z)
        {{0, 0,-1}, {1, 0, 0}, {0, 1, 0}},   // back   (-Z)
    };
    glm::vec3 c(cx, cy, cz);
    glm::vec3 ext(hx, hy, hz);
    for (const Face& f : faces) {
        glm::vec3 center = c + glm::vec3(f.n.x*hx, f.n.y*hy, f.n.z*hz);
        glm::vec3 du(f.du.x*ext.x, f.du.y*ext.y, f.du.z*ext.z);
        glm::vec3 dv(f.dv.x*ext.x, f.dv.y*ext.y, f.dv.z*ext.z);
        uint32_t base = static_cast<uint32_t>(wom.vertices.size());
        auto push = [&](glm::vec3 p, float u, float v) {
            wowee::pipeline::WoweeModel::Vertex vtx;
            vtx.position = p;
            vtx.normal = f.n;
            vtx.texCoord = {u, v};
            wom.vertices.push_back(vtx);
        };
        push(center - du - dv, 0, 0);
        push(center + du - dv, 1, 0);
        push(center + du + dv, 1, 1);
        push(center - du + dv, 0, 1);
        wom.indices.insert(wom.indices.end(),
            {base, base + 1, base + 2, base, base + 2, base + 3});
    }
}

void addFlatBox(wowee::pipeline::WoweeModel& wom,
                glm::vec3 lo, glm::vec3 hi) {
    glm::vec3 c = (lo + hi) * 0.5f;
    glm::vec3 h = (hi - lo) * 0.5f;
    addFlatBox(wom, c.x, c.y, c.z, h.x, h.y, h.z);
}

} // namespace cli
} // namespace editor
} // namespace wowee
