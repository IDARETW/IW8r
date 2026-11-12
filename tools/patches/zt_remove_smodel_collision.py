import io, sys
root = sys.argv[1]
p = root + '/src/convert/iw3_iw8/convert.cpp'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
def rep(a, b):
    global s
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (s.count(a), a[:80])
    s = s.replace(a, b)

rep('''			report["static_model_instances"] = world->smodels.instanceCount;
			report["hidden_static_models"] = hidden;
		}
''', '''			report["static_model_instances"] = world->smodels.instanceCount;
			report["hidden_static_models"] = hidden;
		}

		//   "remove_static_model_collision": [{"model": name, "origin": [x, y, z]}] takes the collision of those static
		//   models out (IW8 Radiant research 88, roadmap 11.5): the instance of that model at that place (within half a
		//   unit) leaves the collision map's static model collision lists. A big map keeps that collision in its
		//   collision tiles instead, which this does not edit: the report says how many were found.
		// Kept alive until the zone is written.
		std::deque<std::vector<StaticModelCollisionInstance>> kept_collision;
		if (edits.contains("remove_static_model_collision"))
		{
			auto* clip = static_cast<clipMap_t*>(find(ASSET_TYPE_CLIPMAP));
			if (!clip)
			{
				throw std::runtime_error(zone_name + " has no collision map");
			}
			struct Wanted
			{
				std::string model;
				vec3_t origin{};
				bool found{};
			};
			std::vector<Wanted> wanted;
			for (const auto& entry : edits.at("remove_static_model_collision"))
			{
				Wanted w;
				w.model = entry.at("model").get<std::string>();
				const auto& o = entry.at("origin");
				if (!o.is_array() || o.size() != 3)
				{
					throw std::runtime_error("remove_static_model_collision: origin is not three numbers");
				}
				for (auto axis = 0; axis < 3; axis++)
				{
					w.origin.v[axis] = o[axis].get<float>();
				}
				wanted.push_back(std::move(w));
			}
			const auto text_of = [&](const scr_string_t name) -> std::string
			{
				const auto index = static_cast<std::size_t>(name);
				return index < zone->script_strings.size() && zone->script_strings[index] ? zone->script_strings[index] : "";
			};
			std::size_t removed = 0;
			for (std::uint32_t l = 0; l < clip->numStaticModelCollisionModelLists; l++)
			{
				auto& list = clip->staticModelCollisionModelLists[l];
				for (std::uint32_t m = 0; m < list.numModels; m++)
				{
					auto& model = list.models[m];
					const auto name = text_of(model.name);
					auto& keep = kept_collision.emplace_back();
					for (std::uint32_t i = 0; i < model.numInstances; i++)
					{
						const auto& instance = model.instances[i];
						bool drop = false;
						for (auto& w : wanted)
						{
							if (w.found || w.model != name)
							{
								continue;
							}
							bool near = true;
							for (auto axis = 0; axis < 3; axis++)
							{
								near &= std::abs(instance.origin.v[axis] - w.origin.v[axis]) <= 0.5f;
							}
							if (near)
							{
								w.found = drop = true;
								break;
							}
						}
						if (drop)
						{
							++removed;
						}
						else
						{
							keep.push_back(instance);
						}
					}
					if (keep.size() != model.numInstances)
					{
						model.numInstances = static_cast<std::uint32_t>(keep.size());
						model.instances = keep.empty() ? nullptr : keep.data();
					}
				}
			}
			report["static_model_collision_asked"] = wanted.size();
			report["static_model_collision_removed"] = removed;
		}
''')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
