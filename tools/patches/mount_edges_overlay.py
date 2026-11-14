import io

# View > Show Compiled Collision also shows the build's mount edges (research 125) as yellow lines: the mapper sees
# where a weapon can be mounted on the built map.

ROOT = 'E:/IW8-Radiant/'


def patch(p, a, b, count=1):
    s = io.open(ROOT + p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == count, (p, s.count(a), a[:70])
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s.replace(a, b))


patch('src/core/collision_preview.h', '''    bool ladder = false;
    std::vector<std::pair<Vec3, Vec3>> edges;  // unique hull edges
};''', '''    bool ladder = false;
    bool mount = false;  // not a hull: the build's mount edges ("mount_edges"), all in one entry, the last
    std::vector<std::pair<Vec3, Vec3>> edges;  // unique hull edges
};''')
patch('src/core/collision_preview.cpp', '''        if (!h.edges.empty()) out.push_back(std::move(h));
    }
    return true;
}
''', '''        if (!h.edges.empty()) out.push_back(std::move(h));
    }
    // The mount edges of the build (research 125): [ax, ay, az, bx, by, bz, ...] each.
    if (const json::Value* list = root->get("mount_edges"); list && list->type == json::Value::Type::Array) {
        CollisionHullEdges m;
        m.mount = true;
        for (const json::Value& e : list->arr) {
            if (e.type != json::Value::Type::Array || e.arr.size() < 6) continue;
            Vec3 a, b;
            for (int i = 0; i < 3; ++i) a[i] = e.arr[size_t(i)].number().value_or(0), b[i] = e.arr[size_t(i) + 3].number().value_or(0);
            m.edges.push_back({a, b});
        }
        if (!m.edges.empty()) out.push_back(std::move(m));
    }
    return true;
}
''')
for p, indent in (('src/viewport/camera_geometry.cpp', '            '), ('src/viewport/scene.cpp', '        ')):
    patch(p, indent + '''// Solid cyan, player clip orange, ladder green, anything else magenta (R in the low byte).
''' + indent + '''const uint32_t c = h.ladder ? 0xff40ff40u : (h.contents & 0x10000u) && !(h.contents & 1u) ? 0xff2090ffu''',
          indent + '''// Solid cyan, player clip orange, ladder green, mount edges yellow, anything else magenta (R in the low byte).
''' + indent + '''const uint32_t c = h.mount ? 0xff20f0ffu : h.ladder ? 0xff40ff40u : (h.contents & 0x10000u) && !(h.contents & 1u) ? 0xff2090ffu''')
patch('src/qtapp/main_window.cpp', '''        else
            ctx_.setStatus(QString("Compiled collision: %1 hulls").arg(ctx_.scene.collisionPreview.size()));''',
      '''        else {
            size_t mount = 0, hulls = 0;
            for (const auto& h : ctx_.scene.collisionPreview) (h.mount ? mount : hulls) += h.mount ? h.edges.size() : 1;
            ctx_.setStatus(QString("Compiled collision: %1 hulls (cyan; clip orange, ladders green), %2 mount edge(s) (yellow)").arg(hulls).arg(mount));
        }''')
patch('tests/collision_preview_tests.cpp', '''    CHECK(!readCollisionHulls("{}", none));''', '''    CHECK(!readCollisionHulls("{}", none));
    // The build's mount edges come as one entry after the hulls.
    std::vector<CollisionHullEdges> withMount;
    CHECK(readCollisionHulls("{\\"brushes\\":[],\\"mount_edges\\":[[0,0,0,0,0,64,0,15944,1],[8,0,40,72,0,40,0,15944,1]]}", withMount));
    CHECK(withMount.size() == 1 && withMount[0].mount && withMount[0].edges.size() == 2);
    if (withMount.size() == 1 && withMount[0].edges.size() == 2) {
        CHECK(withMount[0].edges[0].second.z == 64 && withMount[0].edges[1].first.x == 8 && withMount[0].edges[1].second.x == 72);
    }''')
print('ok')
