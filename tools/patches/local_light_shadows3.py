import io


def patch(p, a, b, n=1):
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == n, (p, s.count(a), a[:80])
    s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


R = 'src/viewport/render.cpp'
patch(R, '''            lightSlots_[size_t(s)].lastUsed = frame_;
        }
    }''', '''            lightSlots_[size_t(s)].lastUsed = frame_;
        }
        // The slots' matrices and lookup constants (t23), when a slot changed.
        if (lightSlotsChanged_ && lightSlotBuf_) {
            std::array<float, kLightSlots * 20> sb{};
            for (int j = 0; j < kLightSlots; ++j) {
                std::memcpy(&sb[size_t(j) * 20], lightSlots_[size_t(j)].vp, 16 * sizeof(float));
                std::memcpy(&sb[size_t(j) * 20 + 16], lightSlots_[size_t(j)].params, 4 * sizeof(float));
            }
            ctx_->UpdateSubresource(lightSlotBuf_, 0, nullptr, sb.data(), 0, 0);
            lightSlotsChanged_ = false;
        }
    }
    lightShadowsDrawn_ = 0;
    for (const LightSlot& sl : lightSlots_) lightShadowsDrawn_ += sl.light >= 0 && sl.drawn ? 1 : 0;''')
print('ok')
